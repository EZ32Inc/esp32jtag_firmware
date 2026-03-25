#include <string.h>
#include <stdlib.h>
#include <fcntl.h>
#include "esp_http_server.h"
#include "esp_chip_info.h"
#include "esp_random.h"
#include "esp_log.h"
#include "esp_vfs.h"
#include "driver/gpio.h"
#include "driver/uart.h"

#include "cJSON.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"
#include "freertos/stream_buffer.h"

#include "uart_websocket.h"
#include "web_server.h"
#define WEB_MOUNT_POINT "/www"

static const char *TAG = "uart_websocket";

#include "storage.h"
#include "esp32jtag_common.h"
#include "port_cfg.h"
#include "types.h"

#define ECHO_TEST					(0)	// echo back test for websocket
#define USE_STREAM_CALLBACK			(0)	// untested: try only when necessary
#define REST_CHECK(a, str, goto_tag, ...) \
    do { \
		if (!(a)) { \
			ESP_LOGE(TAG, "%s(%d): " str, __FUNCTION__, __LINE__, ##__VA_ARGS__); \
		goto goto_tag; \
		} \
	} while (0)
#define CHECK_FILE_EXTENSION(filename, ext) \
	(strcasecmp(&filename[strlen(filename) - strlen(ext)], ext) == 0)

static esp_err_t init_hardware(void);
static void ws_async_send(void *arg);
static void uart_read_task(void *pvParameters);
static esp_err_t websocket_handler(httpd_req_t *req);

#if USE_STREAM_CALLBACK
void vStreamSendCallback(StreamBufferHandle_t xStreamBuffer,
		BaseType_t xIsInsideISR, BaseType_t * const pxPriority);
#endif

volatile bool config_received = false;
static vprintf_like_t original_vprintf = NULL;
volatile bool g_log_monitor_enabled = false;

/* rest server context data structure */
typedef struct rest_server_context {
    char base_path[ESP_VFS_PATH_MAX + 1];
    char scratch[SCRATCH_BUFSIZE];
} rest_server_context_t;

/* response data structure */
typedef struct _resp_data {
    httpd_handle_t hd;
    int fd;
	uint8_t data[UART_BUF_SIZE];
} resp_data_t;

resp_data_t resp_arg;				// response data
uint8_t uart_data[UART_BUF_SIZE];	// uart data
StreamBufferHandle_t xDataBuffer;	// stream buffer from UART to WS
QueueHandle_t s_uart_queue;

static int custom_log_vprintf(const char *format, va_list args) {
    if (g_log_monitor_enabled) {
        /* Recursion guard: prevent WS-send errors from re-triggering this handler */
        static int in_log = 0;
        if (!in_log) {
            in_log = 1;
            char temp_log_buffer[256];
            int written = vsnprintf(temp_log_buffer, sizeof(temp_log_buffer), format, args);
            if (written > 0) {
                if (xDataBuffer) xStreamBufferSend(xDataBuffer, temp_log_buffer, written, 0);
                if (resp_arg.fd > 0 && resp_arg.hd != NULL) {
                    httpd_queue_work(resp_arg.hd, ws_async_send, (void *)&resp_arg);
                }
            }
            in_log = 0;
        }
    }

    // Always call original to keep serial output
    if (original_vprintf) {
        return original_vprintf(format, args);
    }
    return vprintf(format, args);
}

static void set_uart_params(uart_config_t *uart_config) {
    char *val = NULL;

    // Baud Rate
    if (storage_alloc_and_read(UART_BAUD_KEY, &val) == ESP_OK && val) {
        uart_config->baud_rate = atoi(val);
        free(val); val = NULL;
    } else {
        uart_config->baud_rate = 115200;
    }

    // Data Bits
    if (storage_alloc_and_read(UART_DATA_BITS_KEY, &val) == ESP_OK && val) {
        int dbits = atoi(val);
        if (dbits == 5) uart_config->data_bits = UART_DATA_5_BITS;
        else if (dbits == 6) uart_config->data_bits = UART_DATA_6_BITS;
        else if (dbits == 7) uart_config->data_bits = UART_DATA_7_BITS;
        else uart_config->data_bits = UART_DATA_8_BITS;
        free(val); val = NULL;
    } else {
        uart_config->data_bits = UART_DATA_8_BITS;
    }

    // Stop Bits
    if (storage_alloc_and_read(UART_STOP_BITS_KEY, &val) == ESP_OK && val) {
        if (strcmp(val, "1.5") == 0) uart_config->stop_bits = UART_STOP_BITS_1_5;
        else if (strcmp(val, "2") == 0) uart_config->stop_bits = UART_STOP_BITS_2;
        else uart_config->stop_bits = UART_STOP_BITS_1;
        free(val); val = NULL;
    } else {
        uart_config->stop_bits = UART_STOP_BITS_1;
    }

    // Parity
    if (storage_alloc_and_read(UART_PARITY_KEY, &val) == ESP_OK && val) {
        if (strcmp(val, "e") == 0) uart_config->parity = UART_PARITY_EVEN;
        else if (strcmp(val, "o") == 0) uart_config->parity = UART_PARITY_ODD;
        else uart_config->parity = UART_PARITY_DISABLE;
        free(val); val = NULL;
    } else {
        uart_config->parity = UART_PARITY_DISABLE;
    }
}

/*
 * initialize UART port and power state monitor port
 */
static esp_err_t init_hardware() {
	// UART
	uart_config_t uart_config = {
		.baud_rate = 115200,
		.data_bits = UART_DATA_8_BITS,
		.parity = UART_PARITY_DISABLE,
		.stop_bits = UART_STOP_BITS_1,
		.flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
		.source_clk = UART_SCLK_DEFAULT,
	};
    
    set_uart_params(&uart_config);

	ESP_ERROR_CHECK(uart_param_config(UART_PORT_NUM, &uart_config));
	ESP_ERROR_CHECK(uart_set_pin(UART_PORT_NUM, GPIO_UART_TXD, GPIO_UART_RXD,
				UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
	ESP_ERROR_CHECK(uart_driver_install(UART_PORT_NUM, UART_BUF_SIZE,
				UART_BUF_SIZE, 0, NULL, 0));
    ESP_LOGI(TAG, "init UART %d OK: Baud:%d, DB:%d, SB:%d, Parity:%d", 
             UART_PORT_NUM, uart_config.baud_rate, uart_config.data_bits, uart_config.stop_bits, uart_config.parity);
	return ESP_OK;
}

#if 0
/*
 * power control task
 */
static void target_pwr_ctrl_task(void *pvParameters) {
	int gpio_pin;
	gpio_config_t io_config = {};

	if((int)pvParameters == 1) {
		// wake up
		gpio_pin = GPIO_PWR_WAKE;
		ESP_LOGI(TAG, "waking up target");
	} else {
		// shutdown
		gpio_pin = GPIO_PWR_SHDN;
		ESP_LOGI(TAG, "shutting down target");
	}

	// set up the control pin
	io_config.intr_type = GPIO_INTR_DISABLE;
	io_config.mode = GPIO_MODE_OUTPUT;
	io_config.pin_bit_mask = (1ULL << gpio_pin);
	io_config.pull_down_en = 0;
	io_config.pull_up_en = 1;	// prevent glitch ?
	gpio_config(&io_config);

	// 500 msec active low pulse
	gpio_set_level(gpio_pin, 0);
	vTaskDelay( 500 / portTICK_PERIOD_MS);
	gpio_set_level(gpio_pin, 1);

	// reset the pin
	gpio_reset_pin(gpio_pin);

	// kill itself
	vTaskDelete(NULL);
}
#endif

/*
 * async send function, which we put into the httpd work queue
 */
static void ws_async_send(void *arg)
{
    resp_data_t *r = arg;
    httpd_ws_frame_t ws_pkt;
    memset(&ws_pkt, 0, sizeof(httpd_ws_frame_t));
    ws_pkt.type = HTTPD_WS_TYPE_TEXT;

	httpd_handle_t hd = r->hd;
	int fd = r->fd;

    // Check if the server handle is valid
    if (hd == NULL) {
        ESP_LOGE(TAG, "hd is NULL");
        return;
    }

    // Check if the file descriptor is valid (it might have been cleared by close event)
    if (fd <= 0) {
        // This is normal if connection closed while work was in queue. Be silent or debug log.
        // ESP_LOGD(TAG, "fd is invalid/closed: %d, skipping async send", fd);
        return;
    }

    int len = xStreamBufferReceive(xDataBuffer, r->data, sizeof(r->data),
            (TickType_t) 10);
            
    // Only send if we actually received data
    if (len > 0) {
        ws_pkt.len = len;
        ws_pkt.payload = r->data;
        //ESP_LOGI(TAG, "From Buffer: len=%d, payload: %.*s", len, len, ws_pkt.payload);
        esp_err_t send_ret = httpd_ws_send_frame_async(hd, fd, &ws_pkt);
        if (send_ret != ESP_OK) {
            ESP_LOGE(TAG, "httpd_ws_send_frame_async failed with error: %s (fd=%d)", esp_err_to_name(send_ret), fd);
            
            // If the socket is invalid, stop the log monitor to prevent error spam
            if (send_ret == ESP_ERR_INVALID_ARG || send_ret == ESP_ERR_HTTPD_INVALID_REQ) {
                 ESP_LOGW(TAG, "Invalid socket detected in async send. Disabling Log Monitor and clearing session.");
                 g_log_monitor_enabled = false;
                 // We can also clear resp_arg to be safe
                 if (resp_arg.fd == fd) {
                     memset(&resp_arg, 0, sizeof(resp_data_t));
                 }
            }
        }
    }
}

#if 0 //AAron's code to log debug info to webterm
static void ws_async_send(void *arg)
{
    resp_data_t *r = (resp_data_t *)arg;

    httpd_ws_frame_t ws_pkt;
    memset(&ws_pkt, 0, sizeof(httpd_ws_frame_t));
    ws_pkt.type = HTTPD_WS_TYPE_TEXT;

    ws_pkt.len = strlen((const char *)r->data);
    ws_pkt.payload = r->data;

    ESP_LOGI(TAG, "From Buffer: %.*s", ws_pkt.len, ws_pkt.payload);
    httpd_ws_send_frame_async(r->hd, r->fd, &ws_pkt);
    
    free(r);
}

static void uart_read_task(void *pvParameters) {
    resp_data_t client_info;
    
    if (xQueueReceive(s_ws_client_queue, &client_info, portMAX_DELAY) != pdPASS) {
        ESP_LOGE(TAG, "Failed to get first client from queue");
        vTaskDelete(NULL);
    }
    
    while(1) {
        // Read data from the StreamBuffer
        size_t len = xStreamBufferReceive(xDataBuffer, uart_data, (UART_BUF_SIZE - 1), 20 / portTICK_PERIOD_MS);
        
        if (len > 0) {
            resp_data_t *resp = (resp_data_t *)malloc(sizeof(resp_data_t));
            if (resp == NULL) {
                ESP_LOGE(TAG, "Failed to allocate memory for async response");
                continue;
            }
            
            resp->hd = client_info.hd;
            resp->fd = client_info.fd;
            memcpy(resp->data, uart_data, len);
            resp->data[len] = '\0';
            
            esp_err_t ret = httpd_queue_work(resp->hd, ws_async_send, (void *)resp);
            if(ret != ESP_OK) {
                ESP_LOGE(TAG, "httpd_queue_work failed with error %s, waiting for new client.", esp_err_to_name(ret));
                free(resp);
                if (xQueueReceive(s_ws_client_queue, &client_info, portMAX_DELAY) != pdPASS) {
                    ESP_LOGE(TAG, "Failed to get next client from queue");
                    vTaskDelete(NULL);
                }
            }
        }
    }
}
#else
/*
 * UART incoming data handling task.
 * Note: could be unified with the general UART event handler in the future.
 */
static void uart_read_task(void *pvParameters) {
    ESP_LOGI(TAG, "In uart_read_task()");

    httpd_ws_frame_t ws_pkt;
	memset(&ws_pkt, 0, sizeof(httpd_ws_frame_t));
	ws_pkt.type = HTTPD_WS_TYPE_TEXT;

	while(1) {
		// wait for UART RX input
		int len = uart_read_bytes(UART_PORT_NUM, uart_data, (UART_BUF_SIZE -1),
				20 / portTICK_PERIOD_MS);
		// need to terminate the string
		uart_data[len] = 0x00;

		if(len) {
			ESP_LOGI(TAG, "From UART: len=%d, data: %.*s", len, len, uart_data);
			// push data into stream
            xStreamBufferSend(xDataBuffer, uart_data, len, (TickType_t) 10);
            if (resp_arg.fd > 0) {
                esp_err_t ret = httpd_queue_work(resp_arg.hd, ws_async_send, (void *)&resp_arg);
                if(ret != ESP_OK) {
                    ESP_LOGE(TAG, "httpd_queue_work failed");
                }
            }
		}
	}
}

#endif
void uart_write_task(void *pvParameters) {
    char *message;
    for(;;) {
        if (xQueueReceive(s_uart_queue, &message, portMAX_DELAY) == pdPASS) {
            // Check for control commands
            if (strncmp(message, "LOG_ON", 6) == 0) {
                 g_log_monitor_enabled = true;
                 ESP_LOGI(TAG, "Log Monitor ENABLED");
                 free(message);
                 continue;
            }
            if (strncmp(message, "LOG_OFF", 7) == 0) {
                 g_log_monitor_enabled = false;
                 ESP_LOGI(TAG, "Log Monitor DISABLED");
                 free(message);
                 continue;
            }

            ESP_LOGI(TAG, "Input to send: %s", message);
            
            // Write to UART
            int len = strlen(message);
            if(len>0){
                int txBytes = uart_write_bytes(UART_PORT_NUM, message, len);
                if (txBytes > 0) {
                    ESP_LOGI(TAG, "Sent %d bytes to UART", txBytes);
                } else {
                    ESP_LOGE(TAG, "Failed to send to UART");
                }

                if (strcmp(message, "stop\n") == 0) {
                    config_received = true;
                    ESP_LOGI(TAG, "Configuration loop will now stop.");
                }
            }
            free(message);
        }
    }
}

#if 0
static esp_err_t set_content_type_from_file(httpd_req_t *req, const char *filepath)
{
    const char *type = "text/plain";
    if (CHECK_FILE_EXTENSION(filepath, ".html")) {
        type = "text/html";
    } else if (CHECK_FILE_EXTENSION(filepath, ".js")) {
        type = "text/javascript";
    } else if (CHECK_FILE_EXTENSION(filepath, ".css")) {
        type = "text/css";
    } else if (CHECK_FILE_EXTENSION(filepath, ".png")) {
        type = "image/png";
    } else if (CHECK_FILE_EXTENSION(filepath, ".ico")) {
        type = "image/x-icon";
    } else if (CHECK_FILE_EXTENSION(filepath, ".svg")) {
        type = "image/svg+xml";
    }
    return httpd_resp_set_type(req, type);
}

esp_err_t power_post_handler(httpd_req_t *req)
{
    int total_len = req->content_len;
    int cur_len = 0;
    char *buf = ((rest_server_context_t *)(req->user_ctx))->scratch;
    int received = 0;
    if (total_len >= SCRATCH_BUFSIZE) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "content too long");
        return ESP_FAIL;
    }
    while (cur_len < total_len) {
        received = httpd_req_recv(req, buf + cur_len, total_len);
        if (received <= 0) {
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to post control value");
            return ESP_FAIL;
        }
        cur_len += received;
    }
    buf[total_len] = '\0';

    cJSON *root = cJSON_Parse(buf);
    char *state = cJSON_GetObjectItem(root, "power")->valuestring;

    if( strncmp((const char*)state, "on", sizeof(state)) == 0 ||
            strncmp((const char*)state, "ON", sizeof(state)) == 0 ||
            strncmp((const char*)state, "On", sizeof(state)) == 0) {
        httpd_resp_sendstr(req, "Waking up target device");
        xTaskCreate(target_pwr_ctrl_task, "target_pwr_ctrl_task", 2048, (void*)1, 10, NULL);
    } else if( strncmp((const char*)state, "off", sizeof(state)) == 0 ||
            strncmp((const char*)state, "Off", sizeof(state)) == 0 ||
            strncmp((const char*)state, "Off", sizeof(state)) == 0) {
        httpd_resp_sendstr(req, "Shutting down target device");
        xTaskCreate(target_pwr_ctrl_task, "target_pwr_ctrl_task", 2048, (void*)0, 10, NULL);
    } else {
        httpd_resp_sendstr(req, "Invalid power state detected");
    }

    ESP_LOGI(TAG, "Power control: %s", state);

    cJSON_Delete(root);
    return ESP_OK;
}

esp_err_t power_get_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "application/json");
    cJSON *root = cJSON_CreateObject();

    gpio_set_pull_mode(GPIO_UART_RXD, GPIO_PULLDOWN_ONLY);
    int state = gpio_get_level(GPIO_UART_RXD);
    gpio_set_pull_mode(GPIO_UART_RXD, GPIO_PULLUP_ONLY);

    if(state == 1) {
        cJSON_AddStringToObject(root, "power", "on");
    } else {
        cJSON_AddStringToObject(root, "power", "off");
    }
    const char *power_state = cJSON_Print(root);
    ESP_LOGI(TAG, "Power state: %s", power_state);

    httpd_resp_sendstr(req, power_state);
    free((void *)power_state);
    cJSON_Delete(root);

    return ESP_OK;
}
#endif

esp_err_t websocket_handler(httpd_req_t *req)
{
    if (req->method == HTTP_GET) {
        ESP_LOGI(TAG, "Handshake done, new connection opened");
        // New client connects
        resp_arg.hd = req->handle;
        int new_fd = httpd_req_to_sockfd(req);
        if (new_fd > 0) {
            resp_arg.fd = new_fd;
            ESP_LOGI(TAG, "New WS connection: fd=%d", new_fd);
            
            // Clear any old data from the stream buffer to prevent blasting old logs
            xStreamBufferReset(xDataBuffer);
        } else {
            ESP_LOGE(TAG, "Failed to get sockfd for new connection");
            return ESP_FAIL;
        }

        // Reset log monitor state on new connection to avoid mismatch
        g_log_monitor_enabled = false; 
        return ESP_OK;

    } else {
        //ESP_LOGI(TAG, "Frame/Other req: method=%d", req->method);
    }

    // Removed blocking return to allow frame processing code below to run
    //return ESP_OK;

    httpd_ws_frame_t ws_pkt;
    memset(&ws_pkt, 0, sizeof(httpd_ws_frame_t));
    
    // Get frame header
    esp_err_t ret = httpd_ws_recv_frame(req, &ws_pkt, 0);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "httpd_ws_recv_frame failed to get frame info with %d", ret);
        return ret;
    }

    //ESP_LOGI(TAG, "WS Frame: type=%d, len=%d", ws_pkt.type, ws_pkt.len);

    if (ws_pkt.type == HTTPD_WS_TYPE_CLOSE) {
        int close_fd = httpd_req_to_sockfd(req);
        ESP_LOGI(TAG, "Connection closed (fd=%d)", close_fd);
        
        // Only clear global state if the closing connection is the active one
        if (resp_arg.fd == close_fd) {
             g_log_monitor_enabled = false;
             memset(&resp_arg, 0, sizeof(resp_data_t));
             ESP_LOGI(TAG, "Cleared active session state");
        } else {
             ESP_LOGW(TAG, "Closed connection (fd=%d) does not match active session (fd=%d), ignoring cleanup", close_fd, resp_arg.fd);
        }
        return ESP_OK;
    }

    if (ws_pkt.len) {
        char *message_buffer = (char *)malloc(ws_pkt.len + 1);
        if (message_buffer == NULL) {
            ESP_LOGE(TAG, "Failed to allocate memory for payload");
            return ESP_ERR_NO_MEM;
        }

        ws_pkt.payload = (uint8_t *)message_buffer;
        ret = httpd_ws_recv_frame(req, &ws_pkt, ws_pkt.len);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "httpd_ws_recv_frame failed to receive payload with %d", ret);
            free(message_buffer);
            return ret;
        }
        
        message_buffer[ws_pkt.len] = '\0';
        //ESP_LOGI(TAG, "WS Recv Payload: %s", message_buffer);

        /* Handle control commands immediately — independent of whether
         * uart_write_task is running (it only starts when Port B is in
         * UART mode, but the Log Monitor must work in any Port B mode). */
        if (strcmp(message_buffer, "LOG_ON") == 0) {
            g_log_monitor_enabled = true;
            xStreamBufferReset(xDataBuffer);   /* discard stale buffered data */
            ESP_LOGI(TAG, "Log Monitor ENABLED");
            free(message_buffer);
            return ESP_OK;
        }
        if (strcmp(message_buffer, "LOG_OFF") == 0) {
            g_log_monitor_enabled = false;
            ESP_LOGI(TAG, "Log Monitor DISABLED");
            free(message_buffer);
            return ESP_OK;
        }

        /* All other messages are UART data — forward to uart_write_task */
        if (s_uart_queue == NULL || xQueueSend(s_uart_queue, &message_buffer, 0) != pdTRUE) {
            ESP_LOGE(TAG, "Failed to send message to UART write queue");
            free(message_buffer);
        } else {
             //ESP_LOGI(TAG, "Enqueued message for UART TX");
        }
    }
    return ESP_OK;
}

esp_err_t uart_websocket_add_handlers(httpd_handle_t server) {

    // If USB UART is selected, return and do not initialize Web UART
    if (g_app_params.uart_port_sel == 0) {
        ESP_LOGI(TAG, "UART Web Terminal disabled (USB mode selected).");
        return ESP_OK;
    }
    // Else (1), continue to initialize Web UART

	ESP_ERROR_CHECK(init_hardware());
    ESP_LOGI(TAG, "init UART %d done", UART_PORT_NUM);

    REST_CHECK(server, "Invalid server handle", err);

    s_uart_queue = xQueueCreate(10, sizeof(char*));
    REST_CHECK(s_uart_queue != NULL, "UART queue creation failed", err);

    original_vprintf = esp_log_set_vprintf(custom_log_vprintf);

    // Allocate StreamBuffer in PSRAM
    xDataBuffer = xStreamBufferCreateWithCaps(UART_BUF_SIZE * 2, 1, MALLOC_CAP_SPIRAM);
    REST_CHECK(xDataBuffer != NULL, "Stream buffer creation failed", err);

    // Allocate REST context in PSRAM
    rest_server_context_t *rest_context = heap_caps_calloc(
        1, sizeof(rest_server_context_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    // If we failed to make the context, that's a real error
    REST_CHECK(rest_context, "No memory for rest context", err);

    strlcpy(rest_context->base_path, WEB_MOUNT_POINT, sizeof(rest_context->base_path));
#if 0
    ESP_LOGI(TAG, "aliDbg sizeof(rest_server_context_t)=%d, sizeof(base_path)=%d sizeof(resp_data_t)=%d",
             sizeof(rest_server_context_t),
             sizeof(rest_context->base_path),
             sizeof(resp_data_t));
#endif

#if 0
    s_ws_client_queue = xQueueCreate(5, sizeof(resp_data_t));
    REST_CHECK(s_ws_client_queue != NULL, "WS client queue creation failed", err);

    httpd_uri_t power_post_uri = {
        .uri = "/api/v1/pwrctrl",
        .method = HTTP_POST,
        .handler = power_post_handler,
        .user_ctx = rest_context
    };
    httpd_register_uri_handler(server, &power_post_uri);

    httpd_uri_t power_get_uri = {
        .uri = "/api/v1/pwrstate",
        .method = HTTP_GET,
        .handler = power_get_handler,
        .user_ctx = rest_context
    };
    httpd_register_uri_handler(server, &power_get_uri);
#endif

	// URI hander for websocket
    httpd_uri_t websocket_uri = {
        .uri = "/ws",
        .method = HTTP_GET,
        .handler = websocket_handler,
        .user_ctx = rest_context,
		.is_websocket = true
    };
    httpd_register_uri_handler(server, &websocket_uri);

    if (gbl_pb_cfg == PB_UART_SRESET_VTARGET) {
        ESP_LOGI(TAG, "Starting UART tasks...");
        xTaskCreate(uart_read_task, "uart_read_task", 8192, NULL, 10, NULL);
        xTaskCreate(uart_write_task, "uart_write_task", 4096, NULL, 10, NULL);
    }

    return ESP_OK;

err:
    if (s_uart_queue) vQueueDelete(s_uart_queue);
    if (xDataBuffer) vStreamBufferDelete(xDataBuffer);
    return ESP_FAIL;
}
