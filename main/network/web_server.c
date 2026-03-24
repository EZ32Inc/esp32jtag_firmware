#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <sys/stat.h>
#include <sys/unistd.h>
#include <time.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_http_server.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "driver/spi_master.h"
#include "freertos/semphr.h"
#include "esp_timer.h"

#include <cJSON.h>

#include "web_server.h"
#include "network.h"
#include "descriptors.h"
#include "storage.h"
#include "network_mngr_ota.h"
#include "types.h"
#include "uart_websocket.h"
#include "../esp32jtag_common.h"
#include "../ice40up5k/ice.h"
#include "../version_info.h"
#include "version.h"        /* BM FIRMWARE_VERSION from blackmagic_esp32 component */
#include "mbedtls/base64.h"

static esp_err_t check_auth(httpd_req_t *req) {
    char *buf = NULL;
    size_t buf_len = 0;

    buf_len = httpd_req_get_hdr_value_len(req, "Authorization") + 1;
    if (buf_len > 1) {
        buf = calloc(1, buf_len);
        if (httpd_req_get_hdr_value_str(req, "Authorization", buf, buf_len) == ESP_OK) {
            if (strncmp(buf, "Basic ", 6) == 0) {
                char *b64 = buf + 6;
                unsigned char decoded[64] = {0};
                size_t out_len = 0;
                mbedtls_base64_decode(decoded, sizeof(decoded) - 1, &out_len, (unsigned char *)b64, strlen(b64));
                decoded[out_len] = '\0';

                char *user_nvs = NULL;
                char *pass_nvs = NULL;
                char expected[128] = {0};

                storage_alloc_and_read(WEB_USER_KEY, &user_nvs);
                storage_alloc_and_read(WEB_PASS_KEY, &pass_nvs);
                snprintf(expected, sizeof(expected), "%s:%s",
                         user_nvs ? user_nvs : "admin",
                         pass_nvs ? pass_nvs : "admin");
                free(user_nvs);
                free(pass_nvs);

                if (strcmp((char *)decoded, expected) == 0) {
                    free(buf);
                    return ESP_OK;
                }
            }
        }
        free(buf);
    }

    httpd_resp_set_hdr(req, "WWW-Authenticate", "Basic realm=\"ESP32\"");
    httpd_resp_send_err(req, HTTPD_401_UNAUTHORIZED, "Authentication Required");
    return ESP_FAIL;
}

#ifndef MIN
#define MIN(a, b) (((a) < (b)) ? (a) : (b))
#endif

//#define FPGA_CFG_TEST 1

extern void start_capture(bool without_trigger);
extern void read_capture_status(void);

#define DEFAULT_SAMPLE_RATE_HZ  264000000
uint32_t gbl_sample_rate = DEFAULT_SAMPLE_RATE_HZ;
uint8_t gbl_sample_rate_reg = 255;
bool gbl_trigger_enabled = true;
bool gbl_trigger_mode_or = true;
bool gbl_capture_internal_test_signal = false;
trigger_edge_t gbl_channel_triggers[16] = {TRIGGER_DISABLED};


//defined in main.c for comm between webserver and ice.c data capture
extern SemaphoreHandle_t capture_start_semaphore;
extern SemaphoreHandle_t capture_done_semaphore;

static const char *TAG = "web-server";

extern app_params_t g_app_params;
extern uint8_t ICE_FPGA_Config(const uint8_t *bitmap, uint32_t size);

#if 0
// A placeholder/simulation function to be replaced by actual data acquisition logic
// Example: void get_logic_analyzer_data(int num_channels, int num_samples, uint8_t **data_out);
static void get_random_logic_analyzer_data(int num_channels, int num_samples, cJSON *data_array) {
    srand(time(NULL)); // Seed the random number generator
    for (int i = 0; i < num_samples; i++) {
        cJSON *sample_array = cJSON_CreateArray();
        for (int j = 0; j < num_channels; j++) {
            cJSON_AddItemToArray(sample_array, cJSON_CreateNumber(rand() % 2));
        }
        cJSON_AddItemToArray(data_array, sample_array);
    }
}
#endif

extern uint8_t* psram_buffer;
extern void capture_data();
#define CAP_DATA_START (4)
#define HTTP_HEADER_SIZE_EST (250) // Estimate HTTP headers at 250 bytes
esp_err_t capture_data_handler(httpd_req_t *req)
{
    if (check_auth(req) != ESP_OK) return ESP_OK;
    ESP_LOGI(TAG, "=== capture_data_handler() called ===");
    ESP_LOGI(TAG, "capture_data_handler: Calling read_and_return_capture()...");
    
    // Call read_and_return_capture() to get the captured data
    extern void read_and_return_capture(void);
    read_and_return_capture();
    
    ESP_LOGI(TAG, "capture_data_handler: Data ready, sending to browser...");

    uint64_t start_time = esp_timer_get_time();
    esp_err_t ret = httpd_resp_send(req, (const char *)(psram_buffer + CAP_DATA_START), ICE_CAPTURE_BUFFER_SIZE - CAP_DATA_START);
    uint64_t end_time = esp_timer_get_time();

    // Check for errors first
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "httpd_resp_send failed with error: %d", ret);
        return ret;
    }

    uint64_t duration_us = end_time - start_time;
    // Continuation from step 1
    size_t total_bytes_sent = ICE_CAPTURE_BUFFER_SIZE - CAP_DATA_START + HTTP_HEADER_SIZE_EST;

    if (duration_us > 0) {
        // Calculate throughput in Mbps
        // Formula: (total_bytes_sent * 8 bits/byte) / duration_us (microseconds) / 1000000 (micro to mega) * 1000000 (seconds to us)
        // Simplified: (total_bytes_sent * 8) / duration_us
        // Wait, the simplified one is wrong for Mbps, let's stick to the clear one.

        // Convert duration to seconds for clear math
        double duration_s = (double)duration_us / 1000000.0;

        // Bytes per second (B/s)
        double bytes_per_sec = (double)total_bytes_sent / duration_s;

        // Megabits per second (Mbps)
        double mbps = (bytes_per_sec * 8.0) / 1000000.0;

        ESP_LOGI(TAG, "Data sent: %u bytes (+%d header)", ICE_CAPTURE_BUFFER_SIZE -CAP_DATA_START, HTTP_HEADER_SIZE_EST);
        ESP_LOGI(TAG, "Transfer time: %llu us (%.3f ms)", duration_us, duration_us / 1000.0);
        ESP_LOGI(TAG, "Calculated Wi-Fi Speed: %.2f Mbps", mbps);
    } else {
        ESP_LOGW(TAG, "Duration was zero or negative. Cannot calculate speed.");
    }

    return ret;
}

esp_err_t logic_analyzer_handler(httpd_req_t *req) {
    if (check_auth(req) != ESP_OK) return ESP_OK;
    extern const unsigned char logic_analyzer_start[] asm("_binary_logic_analyzer_html_start");
    extern const unsigned char logic_analyzer_end[]   asm("_binary_logic_analyzer_html_end");
    const size_t logic_analyzer_size = (logic_analyzer_end - logic_analyzer_start);

    return httpd_resp_send(req, (const char *)logic_analyzer_start, logic_analyzer_size);
}

esp_err_t help_handler(httpd_req_t *req) {
    extern const unsigned char help_start[] asm("_binary_help_html_start");
    extern const unsigned char help_end[]   asm("_binary_help_html_end");
    const size_t help_size = (help_end - help_start);

    return httpd_resp_send(req, (const char *)help_start, help_size);
}

esp_err_t website_handler(httpd_req_t *req) {
    if (check_auth(req) != ESP_OK) return ESP_OK;
    extern const unsigned char website_html_start[] asm("_binary_website_html_start");
    extern const unsigned char website_html_end[]   asm("_binary_website_html_end");
    const size_t website_html_size = (website_html_end - website_html_start);

    return httpd_resp_send(req, (const char *)website_html_start, website_html_size);
}

esp_err_t ez32_handler(httpd_req_t *req)
{
    extern const unsigned char ez32_svg_start[] asm("_binary_ez32_svg_start");
    extern const unsigned char ez32_svg_end[]   asm("_binary_ez32_svg_end");
    const size_t ez32_size = (ez32_svg_end - ez32_svg_start);

    httpd_resp_set_type(req, "image/svg+xml");

    return httpd_resp_send(req, (const char *)ez32_svg_start, ez32_size);
}

esp_err_t favicon_handler(httpd_req_t *req)
{
    extern const unsigned char favicon_ico_start[] asm("_binary_favicon_ico_start");
    extern const unsigned char favicon_ico_end[]   asm("_binary_favicon_ico_end");
    const size_t favicon_ico_size = (favicon_ico_end - favicon_ico_start);

    httpd_resp_set_type(req, "image/x-icon");

    return httpd_resp_send(req, (const char *)favicon_ico_start, favicon_ico_size);
}

static cJSON *set_handler_start(httpd_req_t *req, char *json_buf, uint32_t json_buf_sz)
{
    memset(json_buf, 0x00, json_buf_sz);

    /* Read data received in the request */
    int ret = httpd_req_recv(req, json_buf, json_buf_sz);
    if (ret <= 0) {
        if (ret == HTTPD_SOCK_ERR_TIMEOUT) {
            httpd_resp_send_408(req);
        }
        return NULL;
    }

    ESP_LOGI(TAG, "json : %s", json_buf);

    cJSON *root = cJSON_ParseWithLength(json_buf, json_buf_sz);
    if (root == NULL) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Json parse error");
        return NULL;
    }
    return root;
}

#ifdef FPGA_CFG_TEST
// Define a buffer size for reading chunks of data
#define BUFF_SIZE 1024

// This is the main handler function for your file upload
static esp_err_t fpga_loader_handler(httpd_req_t *req) {
    //esp_err_t err = ESP_OK;
    char *buf = NULL;
    int remaining = req->content_len;
    int received_bytes = 0;
    
    // --- Header and Boundary Parsing ---
    size_t len = httpd_req_get_hdr_value_len(req, "Content-Type");
    if (len == 0) {
        ESP_LOGE("FPGA_LOADER", "Content-Type header not found");
        return ESP_FAIL;
    }
    
    char *ctype_hdr = (char *)malloc(len + 1);
    if (ctype_hdr == NULL) {
        ESP_LOGE("FPGA_LOADER", "Failed to allocate memory for ctype header");
        return ESP_FAIL;
    }
    
    httpd_req_get_hdr_value_str(req, "Content-Type", ctype_hdr, len + 1);

    
    char *boundary_str = strstr(ctype_hdr, "boundary=");
    if (boundary_str == NULL) {
        ESP_LOGE("FPGA_LOADER", "Boundary not found in Content-Type header");
        free(ctype_hdr);
        return ESP_FAIL;
    }
    
    boundary_str += strlen("boundary=");

    ESP_LOGI("FPGA_LOADER", "boundary_str =%s ctype_hdr=%s len=%d",boundary_str, ctype_hdr,len); 
#if 0
    char boundary_line[128];
    sprintf(boundary_line, "--%s\r\n", boundary_str);
    //size_t boundary_line_len = strlen(boundary_line);

    char boundary_end[128];
    sprintf(boundary_end, "--%s--\r\n", boundary_str);
    //size_t boundary_end_len = strlen(boundary_end);
#endif
    free(ctype_hdr);

    // Read the request body to find the file data
    buf = (char *)malloc(BUFF_SIZE);
    if (buf == NULL) {
        ESP_LOGE("FPGA_LOADER", "Failed to allocate buffer");
        return ESP_FAIL;
    }

    // --- Find the Start of the File Data ---
    int read_bytes = httpd_req_recv(req, buf, BUFF_SIZE);
    if (read_bytes <= 0) {
        ESP_LOGE("FPGA_LOADER", "No data received");
        free(buf);
        return ESP_FAIL;
    }
   /* 
    char *start_data = strstr(buf, "\r\n\r\n");
    if (start_data == NULL) {
        ESP_LOGE("FPGA_LOADER", "Start of file data not found");
        free(buf);
        return ESP_FAIL;
    }
    */
    char *start_data = NULL;
    for (int i = 0; i <= read_bytes - 4; ++i) {
        if (memcmp(buf + i, "\r\n\r\n", 4) == 0) {
            start_data = buf + i;
            ESP_LOGI("FPGA_LOADER_a1", "Found start_data");
            break;
        }
    }

    int total_wr = 0;
    // Pointer to the first byte of the file data
    start_data += 4; // Move past the \r\n\r\n

    int bytes_to_write = read_bytes - (start_data - buf);
    /*
    Yes 👍 this line is perfectly valid C, as long as:
    psram_buffer points to a memory region large enough to hold bytes_to_write bytes.
    buf points to at least bytes_to_write valid bytes of source data.
    bytes_to_write is non-negative (well, since it’s size_t, it’s unsigned).*/
    memcpy(psram_buffer+total_wr, start_data, bytes_to_write);

    total_wr += bytes_to_write;

    remaining -= read_bytes;
    received_bytes += bytes_to_write;

    ESP_LOGI(TAG, "remaining=%d received_bytes=%d  bytes_to_write=%d", remaining, received_bytes,  bytes_to_write); 
    //if(bytes_to_write>64){
    //    for(int i =0;i<64;++i){
    //        ESP_LOGI(TAG, "%02x ",psram_buffer[i]);
    //    }
    //}

    char *final_boundary= "------WebKitFormBoundary";
    //sprintf(final_boundary, "%s", "------WebKitFormBoundary");
    size_t final_boundary_len = strlen(final_boundary);

    // --- Continue Reading and Writing the Rest of the File ---
    while (remaining > 0) {
        read_bytes = httpd_req_recv(req, buf, MIN(BUFF_SIZE, remaining));
        if (read_bytes <= 0) {
            ESP_LOGE("FPGA_LOADER", "Failed to receive data from client");
            free(buf);
            return ESP_FAIL;
        }

        //char *end_of_data_pos = strstr(buf, final_boundary);
        //char *end_of_data_pos = strstr(buf, "WebKitFormBoundary");
        //char *end_of_data_pos = strstr(buf, "----");
        //char *end_of_data_pos = strstr(buf, "\x2d\x2d\x2d\x2d\x2d\x2d");
        // Correct approach using a for loop and memcmp
        char *end_of_data_pos = NULL;
        for (int i = 0; i <= read_bytes - final_boundary_len; ++i) {
            if (memcmp(buf + i, final_boundary, final_boundary_len ) == 0) {
                end_of_data_pos = buf + i;
                ESP_LOGI("FPGA_LOADER_aa", "Found end boundary");
                break;
            }
        }
        
        if (end_of_data_pos) {
            bytes_to_write = end_of_data_pos - buf -2; //-2 to remove "\r\n" before final_boundary
            memcpy(psram_buffer+total_wr, buf, bytes_to_write);
            total_wr += bytes_to_write;
            ESP_LOGI("FPGA_LOADER_bb", "Found end boundary, writing final %d bytes read_bytes=%d ", bytes_to_write, read_bytes);
            remaining -= read_bytes;
            received_bytes += read_bytes;
            break;
        }
        else{
            bytes_to_write = read_bytes;
            memcpy(psram_buffer+total_wr, buf, bytes_to_write);
            total_wr += read_bytes;
        }

        remaining -= read_bytes;
        received_bytes += read_bytes;
    }
    
    ESP_LOGI("FPGA_LOADER", "last time: read_bytes=%d total_wr=%d", read_bytes, total_wr);
#if 0
    //for(int i =0;i<read_bytes; ++i){
    //    ESP_LOGI(TAG, "%02x ",buf[i]);
    //    if(i+6<read_bytes && buf[i] == 0x2d && buf[i+1] == 0x2d && buf[i+2] == 0x2d && buf[i+3] == 0x2d && buf[i+4] == 0x2d && buf[i+5] == 0x2d)
    //        ESP_LOGI("FPGA_LOADER", "Found end boundary");
    //}

    //char *end_of_data_pos = strstr(buf, "\x2d\x2d\x2d\x2d\x2d\x2d");
    char *end_of_data_pos = NULL;
    for (int i = 0; i <= read_bytes - final_boundary_len; ++i) {
        if (memcmp(buf + i, final_boundary, final_boundary_len ) == 0) {
            end_of_data_pos = buf + i;
            ESP_LOGI("FPGA_LOADER_c1", "Found end boundary");
            break;
        }
    }

    if (end_of_data_pos) {
        ESP_LOGI("FPGA_LOADER_c2", "Found end boundary");
    }
    else{
        ESP_LOGI("FPGA_LOADER_c2", "Not Found end boundary");
    }
#endif
    ESP_LOGI("FPGA_LOADER", "Finished receiving FPGA bin file, total size: %d total_wr=%d", received_bytes, total_wr);
    
    extern spi_device_handle_t gbl_spi_h1;

    if (spi_device_acquire_bus(gbl_spi_h1, portMAX_DELAY) != ESP_OK) {
        ESP_LOGE("FPGA_LOADER", "Failed to acquire SPI bus");
    //    vTaskDelay(pdMS_TO_TICKS(100));
    }
    else{
#if 1
/*
        extern const unsigned char bitstream_bin_start[] asm("_binary_bitstream_bin_start");
        extern const unsigned char bitstream_bin_end[]   asm("_binary_bitstream_bin_end");
        //const size_t sz = (bitstream_bin_end - bitstream_bin_start);
        //uint8_t cfg_stat = ICE_FPGA_Config(bitstream_bin_start, sz);

        //debug
        int i = 0;
        for(i =0;i<total_wr;++i){
            if(psram_buffer[i] != bitstream_bin_start[i]){
                ESP_LOGE("FPGA_LOADER", "data is different!!! i=%d 0x%02x vs  %02x",i, psram_buffer[i], bitstream_bin_start[i]);
                break;
            }
        }

        if(i == total_wr){
            ESP_LOGW("FPGA_LOADER", "data is the same!");
        }
*/
       // memcpy(psram_buffer, bitstream_bin_start,total_wr);

        uint8_t cfg_stat = ICE_FPGA_Config(psram_buffer, total_wr);
        // Send success response
        if(cfg_stat)
#else
       extern esp_err_t load_fpga(void);
       if(ESP_OK != load_fpga())
#endif
            httpd_resp_sendstr(req, "FPGA CFG Failed");
        else
            httpd_resp_sendstr(req, "FPGA CFG successful");
    }

    // Release the SPI bus after the transaction is done
    spi_device_release_bus(gbl_spi_h1);

    free(buf);
    return ESP_OK;
}
#endif //#ifdef FPGA_CFG_TEST 

#ifndef FPGA_CFG_TEST 
static esp_err_t ota_upload_handler(httpd_req_t *req) {
    if (check_auth(req) != ESP_OK) return ESP_OK;
    esp_ota_handle_t ota_handle;
    const esp_partition_t *update_partition = esp_ota_get_next_update_partition(NULL);
    esp_err_t err;

    if (!update_partition) {
        ESP_LOGE(TAG, "No OTA partition found");
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "No OTA partition found");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Starting OTA on partition: %s", update_partition->label);

    err = esp_ota_begin(update_partition, OTA_WITH_SEQUENTIAL_WRITES, &ota_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_begin failed (%s)", esp_err_to_name(err));
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OTA begin failed");
        return ESP_FAIL;
    }

    char *buf = (char *)malloc(4096);
    if (buf == NULL) {
        ESP_LOGE(TAG, "Failed to allocate memory for buffer");
        esp_ota_abort(ota_handle);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Memory allocation failed");
        return ESP_FAIL;
    }

    // Get the boundary from the header
    size_t len = httpd_req_get_hdr_value_len(req, "Content-Type");
    if (len == 0) {
        ESP_LOGE(TAG, "Content-Type header not found");
        free(buf);
        esp_ota_abort(ota_handle);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Content-Type header not found");
        return ESP_FAIL;
    }
    char *ctype_hdr = (char *)malloc(len + 1);
    if (ctype_hdr == NULL) {
        ESP_LOGE(TAG, "Failed to allocate memory for ctype header");
        free(buf);
        esp_ota_abort(ota_handle);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Memory allocation failed");
        return ESP_FAIL;
    }
    httpd_req_get_hdr_value_str(req, "Content-Type", ctype_hdr, len + 1);

    char *boundary_str = strstr(ctype_hdr, "boundary=");
    if (boundary_str == NULL) {
        ESP_LOGE(TAG, "Boundary not found in Content-Type header");
        free(buf);
        free(ctype_hdr);
        esp_ota_abort(ota_handle);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Boundary not found");
        return ESP_FAIL;
    }

    boundary_str += strlen("boundary=");
    char boundary_line[128];
    snprintf(boundary_line, sizeof(boundary_line), "--%s", boundary_str);
    int boundary_len = strlen(boundary_line);
    free(ctype_hdr);

    int data_read;
    int total_len = 0;
    bool found_firmware = false;
    
    // Read the entire request body to find the firmware data
    while ((data_read = httpd_req_recv(req, buf, 4096)) > 0) {
        if (!found_firmware) {
            // Check for the boundary line that precedes the file data
            char *firmware_start = NULL;
            // Since we are looking for string in the header part (text), strstr is okay for looking for boundary start
            // BUT, to be consistent and safe, let's use memcmp as well if we were searching largely.
            // However, the header part IS text. The boundary itself is text.
            // The issue is if null bytes appear BEFORE the start boundary (unlikely in HTTP multipart)
            // or if we rely on strstr to find \r\n\r\n

            // Search for boundary line
            for (int i = 0; i <= data_read - boundary_len; i++) {
                if (memcmp(buf + i, boundary_line, boundary_len) == 0) {
                     firmware_start = buf + i;
                     break;
                }
            }

            if (firmware_start != NULL) {
                // Find the double CRLF following the boundary to determine start of data
                char *data_start = NULL;
                // Start searching from where boundary was found
                int search_start_idx = firmware_start - buf;

                // We need to find \r\n\r\n.
                // Note: The multipart header can be larger.

                for (int i = search_start_idx; i <= data_read - 4; i++) {
                     if (memcmp(buf + i, "\r\n\r\n", 4) == 0) {
                         data_start = buf + i + 4;
                         break;
                     }
                }

                if (data_start != NULL) {
                    found_firmware = true;
                    int header_len = data_start - buf;
                    int data_len_in_buf = data_read - header_len;

                    err = esp_ota_write(ota_handle, data_start, data_len_in_buf);
                    if (err != ESP_OK) {
                        ESP_LOGE(TAG, "esp_ota_write failed (%s)", esp_err_to_name(err));
                        free(buf);
                        esp_ota_abort(ota_handle);
                        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OTA write failed");
                        return ESP_FAIL;
                    }
                    total_len += data_len_in_buf;
                }
            }
        } else {
            // Search for the end boundary in the current chunk
            char *end_boundary = NULL;
             for (int i = 0; i <= data_read - boundary_len; i++) {
                if (memcmp(buf + i, boundary_line, boundary_len) == 0) {
                     end_boundary = buf + i;
                     break;
                }
            }

            if (end_boundary != NULL) {
                // We've found the end of the file, write the remaining data
                // The data ends before (end_boundary - 2) which is the \r\n before header
                int final_len = end_boundary - buf - 2;

                // Sanity check
                if (final_len < 0) final_len = 0; // Should not happen if protocol obeyed

                err = esp_ota_write(ota_handle, buf, final_len);
                if (err != ESP_OK) {
                    ESP_LOGE(TAG, "esp_ota_write failed (%s)", esp_err_to_name(err));
                    free(buf);
                    esp_ota_abort(ota_handle);
                    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OTA write failed");
                    return ESP_FAIL;
                }
                total_len += final_len;
                break;
            } else {
                // Regular data chunk
                err = esp_ota_write(ota_handle, buf, data_read);
                if (err != ESP_OK) {
                    ESP_LOGE(TAG, "esp_ota_write failed (%s)", esp_err_to_name(err));
                    free(buf);
                    esp_ota_abort(ota_handle);
                    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OTA write failed");
                    return ESP_FAIL;
                }
                total_len += data_read;
            }
        }
    }
    free(buf);
    
    ESP_LOGI(TAG, "Finished receiving OTA image, total size: %d", total_len);
    err = esp_ota_end(ota_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_end failed (%s)", esp_err_to_name(err));
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OTA end failed");
        return ESP_FAIL;
    }

    err = esp_ota_set_boot_partition(update_partition);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_set_boot_partition failed (%s)", esp_err_to_name(err));
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to set boot partition");
        return ESP_FAIL;
    }

    // Send a success response
    const char *resp = "OTA Update Successful! Rebooting...";
    httpd_resp_sendstr(req, resp);

    // Reboot the device after a short delay
    vTaskDelay(pdMS_TO_TICKS(2000));
    esp_restart();

    return ESP_OK;
}
#endif


static char* get_cjson_string(cJSON *item)
{
    if (!item) {
        return NULL;
    }
    if (cJSON_IsString(item)) {
        return item->valuestring;
    }
    if (cJSON_IsNumber(item)) {
        static char num_str[32]; // Not thread safe, but acceptable here
        snprintf(num_str, sizeof(num_str), "%d", item->valueint);
        return num_str;
    }
    return NULL;
}

esp_err_t set_credentials_handler(httpd_req_t *req) {
    if (check_auth(req) != ESP_OK) return ESP_OK;
    char *json = malloc(req->content_len + 1); // +1 for null terminator
    if (!json) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Memory allocation failed");
        return ESP_FAIL;
    }

    cJSON *root = set_handler_start(req, json, req->content_len + 1);
    free(json); // Free buffer as root now holds the data

    if (!root) return ESP_FAIL;

    cJSON *pacfg = cJSON_GetObjectItem(root, "pacfg");
    cJSON *pbcfg = cJSON_GetObjectItem(root, "pbcfg");
    cJSON *pccfg = cJSON_GetObjectItem(root, "pccfg");
    cJSON *pdcfg = cJSON_GetObjectItem(root, "pdcfg");
    cJSON *targetVoltage = cJSON_GetObjectItem(root, "targetVoltage");
    cJSON *swMcu = cJSON_GetObjectItem(root, "swMcu");
    cJSON *wifiMode = cJSON_GetObjectItem(root, "wifiMode");
    cJSON *ssid = cJSON_GetObjectItem(root, "ssid");
    cJSON *pass = cJSON_GetObjectItem(root, "password");
    cJSON *otaUrl = cJSON_GetObjectItem(root, "otaUrl");
    cJSON *mcuInterface = cJSON_GetObjectItem(root, "mcuInterface");

    cJSON *uartBaud = cJSON_GetObjectItem(root, "uartBaud");
    cJSON *uartDataBits = cJSON_GetObjectItem(root, "uartDataBits");
    cJSON *uartStopBits = cJSON_GetObjectItem(root, "uartStopBits");
    cJSON *uartParity = cJSON_GetObjectItem(root, "uartParity");

    cJSON *uartPortSel = cJSON_GetObjectItem(root, "uartPortSel");
    cJSON *disableUsbDapCom = cJSON_GetObjectItem(root, "disableUsbDapCom");

    char *ssid_str = get_cjson_string(ssid);
    char *pass_str = get_cjson_string(pass);

    // if (!ssid_str || !pass_str) {
    //     cJSON_Delete(root);
    //     httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Json parse error: missing SSID or Password");
    //     return ESP_FAIL;
    // }

    char *pacfg_str = get_cjson_string(pacfg);
    if(pacfg_str && strcmp(pacfg_str, "1") == 0) {
        // Force to Logic Analyzer (0) if SWD/JTAG (1) is requested
        pacfg_str = "0";
    }
    char *pbcfg_str = get_cjson_string(pbcfg);
    char *pccfg_str = get_cjson_string(pccfg);
    char *pdcfg_str = get_cjson_string(pdcfg);
    char *targetVoltage_str = get_cjson_string(targetVoltage);
    char *swMcu_str = get_cjson_string(swMcu);
    char *wifiMode_str = get_cjson_string(wifiMode);
    char *otaUrl_str = get_cjson_string(otaUrl);
    char *mcuInterface_str = get_cjson_string(mcuInterface);

    char *uartBaud_str = get_cjson_string(uartBaud);
    char *uartDataBits_str = get_cjson_string(uartDataBits);
    char *uartStopBits_str = get_cjson_string(uartStopBits);
    char *uartParity_str = get_cjson_string(uartParity);
    char *uartPortSel_str = get_cjson_string(uartPortSel);
    bool disableUsbDapCom_val = false;
    if (disableUsbDapCom && cJSON_IsTrue(disableUsbDapCom)) {
        disableUsbDapCom_val = true;
    }

    // Log the received values (placeholder for actual logic)
    if (pacfg_str) ESP_LOGI(TAG, "Port A Config: %s", get_port_a_description(pacfg_str));
    if (pbcfg_str) ESP_LOGI(TAG, "Port B Config: %s", get_port_b_description(pbcfg_str));
    if (pccfg_str) ESP_LOGI(TAG, "Port C Config: %s", get_port_c_description(pccfg_str));
    if (pdcfg_str) ESP_LOGI(TAG, "Port D Config: %s", get_port_d_description(pdcfg_str));
    if (targetVoltage_str) ESP_LOGI(TAG, "Target IO Voltage: %s", get_target_voltage_description(targetVoltage_str));
    if (swMcu_str) ESP_LOGI(TAG, "Software for MCU Debug: %s", get_sw_mcu_description(swMcu_str));
    if (wifiMode_str) ESP_LOGI(TAG, "WiFi Mode: %s", get_wifi_mode_description(wifiMode_str));
    if (otaUrl_str) ESP_LOGI(TAG, "OTA URL: %s", otaUrl_str);
    if (mcuInterface_str) ESP_LOGI(TAG, "MCU Interface: %s", mcuInterface_str);

    if (uartBaud_str) ESP_LOGI(TAG, "UART Baud: %s", uartBaud_str);
    if (uartDataBits_str) ESP_LOGI(TAG, "UART Data Bits: %s", uartDataBits_str);
    if (uartStopBits_str) ESP_LOGI(TAG, "UART Stop Bits: %s", uartStopBits_str);
    if (uartParity_str) ESP_LOGI(TAG, "UART Parity: %s", uartParity_str);
    if (uartPortSel_str) ESP_LOGI(TAG, "UART Port Sel: %s", uartPortSel_str);
    if (disableUsbDapCom) ESP_LOGI(TAG, "Disable USB DAP: %d", disableUsbDapCom_val);

    if (ssid_str) ESP_LOGI(TAG, "SSID: %s", ssid_str);
    // For security, avoid logging password in plain text in production.
    if (pass_str) ESP_LOGI(TAG, "Password received (for demonstration)");

    // Check if critical Wi-Fi settings have changed
    bool reboot_required = false;
    char *stored_val = NULL;

    // Check Wi-Fi Mode
    bool wifi_changed = false;
    if (wifiMode_str) {
        // Check if runtime mode differs from requested mode (e.g. forced AP fallback vs requested SM)
        bool runtime_mismatch = false;
        if (strcmp(wifiMode_str, "SM") == 0 && g_app_params.mode != APP_MODE_STA) runtime_mismatch = true;
        if (strcmp(wifiMode_str, "AP") == 0 && g_app_params.mode != APP_MODE_AP) runtime_mismatch = true;

        if (storage_alloc_and_read(WIFI_MODE_KEY, &stored_val) == ESP_OK && stored_val) {
            if (strcmp(stored_val, wifiMode_str) != 0 || runtime_mismatch) {
                wifi_changed = true;
                reboot_required = true;
                ESP_LOGI(TAG, "WiFi Mode changed or mismatch: stored=%s, req=%s, runtime_mismatch=%d", stored_val, wifiMode_str, runtime_mismatch);
            }
            free(stored_val); stored_val = NULL;
        } else {
            // No stored value but new value provided implies change (or first set)
            wifi_changed = true;
            reboot_required = true;
        }
    }

    // Determine which keys to use based on the requested mode (AP or SM)
    const char *target_ssid_key = WIFI_SSID_KEY;
    const char *target_pass_key = WIFI_PASS_KEY;
    if (wifiMode_str && strcmp(wifiMode_str, "AP") == 0) {
        target_ssid_key = WIFI_AP_SSID_KEY;
        target_pass_key = WIFI_AP_PASS_KEY;
    }

    // Check SSID & Password (relevant for both STA and AP modes now using distinct keys)
    if (wifiMode_str) {
        bool ssid_changed = false;
        bool pass_changed = false;

        if (storage_alloc_and_read(target_ssid_key, &stored_val) == ESP_OK && stored_val) {
            if (ssid_str && strcmp(stored_val, ssid_str) != 0) {
                ssid_changed = true;
                ESP_LOGI(TAG, "SSID changed for %s", wifiMode_str);
            }
            free(stored_val); stored_val = NULL;
        } else if (ssid_str && strlen(ssid_str) > 0) {
             ssid_changed = true; // First time setting or no previous value
        }

        // Check password - if empty, assume no change intended
        if (pass_str && strlen(pass_str) > 0) {
            if (storage_alloc_and_read(target_pass_key, &stored_val) == ESP_OK && stored_val) {
                if (strcmp(stored_val, pass_str) != 0) {
                    pass_changed = true;
                    ESP_LOGI(TAG, "Password changed for %s", wifiMode_str);
                }
                free(stored_val); stored_val = NULL;
            } else {
                pass_changed = true; // First time setting password
            }
        }

        if (ssid_changed || pass_changed) {
             wifi_changed = true;
             reboot_required = true;
        }
    }


    // Check for Port Configuration Changes
    if (pacfg_str && atoi(pacfg_str) != gbl_pa_cfg) {
        ESP_LOGI(TAG, "Port A config changed: %d -> %s", gbl_pa_cfg, pacfg_str);
        reboot_required = true;
    }
    if (pbcfg_str && atoi(pbcfg_str) != gbl_pb_cfg) {
        ESP_LOGI(TAG, "Port B config changed: %d -> %s", gbl_pb_cfg, pbcfg_str);
        reboot_required = true;
    }
    if (pccfg_str && atoi(pccfg_str) != gbl_pc_cfg) {
        ESP_LOGI(TAG, "Port C config changed: %d -> %s", gbl_pc_cfg, pccfg_str);
        reboot_required = true;
    }
    if (pdcfg_str && atoi(pdcfg_str) != gbl_pd_cfg) {
        ESP_LOGI(TAG, "Port D config changed: %d -> %s", gbl_pd_cfg, pdcfg_str);
        reboot_required = true;
    }

    // MCU Interface is fixed to GPIO — ignore incoming value, ensure correct value stored
    {
        char *stored_mcu_if = NULL;
        if (storage_alloc_and_read(MCU_INTERFACE_KEY, &stored_mcu_if) == ESP_OK && stored_mcu_if) {
            if (strcmp(stored_mcu_if, "GPIO") != 0) {
                ESP_LOGI(TAG, "MCU Interface forced to GPIO (was %s)", stored_mcu_if);
                reboot_required = true;
            }
            free(stored_mcu_if);
        } else {
            reboot_required = true;
        }
    }

    // UART Port Selection is fixed to 1 (Web Terminal) — ignore incoming value
    {
        char *stored_uart_psel = NULL;
        if (storage_alloc_and_read(UART_PORT_SEL_KEY, &stored_uart_psel) == ESP_OK && stored_uart_psel) {
            if (strcmp(stored_uart_psel, "1") != 0) {
                ESP_LOGI(TAG, "UART Port Selection forced to Web Terminal (was %s)", stored_uart_psel);
                reboot_required = true;
            }
            free(stored_uart_psel);
        } else {
            reboot_required = true;
        }
    }

    // Check for UART changes
    if (uartBaud_str || uartDataBits_str || uartStopBits_str || uartParity_str) {
        // For simplicity, any non-null UART setting request is treated as a potential change requiring reboot/reinit.
        // In a more complex implementation, we could check against stored values, but reboot is safe.
        ESP_LOGI(TAG, "UART settings updated. Reboot required.");
        reboot_required = true;
    }

    // Disable USB DAP is fixed to true — ignore incoming value
    {
        char *stored_disable_dap = NULL;
        if (storage_alloc_and_read(DISABLE_USB_DAP_KEY, &stored_disable_dap) == ESP_OK && stored_disable_dap) {
            if (strcmp(stored_disable_dap, "1") != 0) {
                ESP_LOGI(TAG, "Disable USB DAP forced to enabled (was %s)", stored_disable_dap);
                reboot_required = true;
            }
            free(stored_disable_dap);
        } else {
            ESP_LOGI(TAG, "Disable USB DAP set for the first time (forced enabled).");
            reboot_required = true;
        }
    }

    // storage write the configurations
    storage_handle_t h;
    if (storage_open_session(&h) == ESP_OK) {
        if (pacfg_str) storage_write_session(h, PORT_A_CFG_KEY, pacfg_str, strlen(pacfg_str));
        if (pbcfg_str) storage_write_session(h, PORT_B_CFG_KEY, pbcfg_str, strlen(pbcfg_str));
        if (pccfg_str) storage_write_session(h, PORT_C_CFG_KEY, pccfg_str, strlen(pccfg_str));
        if (pdcfg_str) storage_write_session(h, PORT_D_CFG_KEY, pdcfg_str, strlen(pdcfg_str));
        if (targetVoltage_str) storage_write_session(h, TARGET_VOLTAGE_KEY, targetVoltage_str, strlen(targetVoltage_str));
        if (swMcu_str) storage_write_session(h, SW_MCU_KEY, swMcu_str, strlen(swMcu_str));
        if (wifi_changed) {
            if (wifiMode_str) storage_write_session(h, WIFI_MODE_KEY, wifiMode_str, strlen(wifiMode_str));
            if (ssid_str) storage_write_session(h, target_ssid_key, ssid_str, strlen(ssid_str));
            // Only write password if provided (non-empty), preserving existing if left blank
            if (pass_str && strlen(pass_str) > 0) storage_write_session(h, target_pass_key, pass_str, strlen(pass_str));
        }

        if (otaUrl_str) storage_write_session(h, OTA_URL_KEY, otaUrl_str, strlen(otaUrl_str));
        storage_write_session(h, MCU_INTERFACE_KEY, "GPIO", 4);

        if (uartBaud_str) storage_write_session(h, UART_BAUD_KEY, uartBaud_str, strlen(uartBaud_str));
        if (uartDataBits_str) storage_write_session(h, UART_DATA_BITS_KEY, uartDataBits_str, strlen(uartDataBits_str));
        if (uartStopBits_str) storage_write_session(h, UART_STOP_BITS_KEY, uartStopBits_str, strlen(uartStopBits_str));
        if (uartParity_str) storage_write_session(h, UART_PARITY_KEY, uartParity_str, strlen(uartParity_str));
        storage_write_session(h, UART_PORT_SEL_KEY, "1", 1);
        storage_write_session(h, DISABLE_USB_DAP_KEY, "1", 1);

        storage_close_session(h);
    }

    cJSON_Delete(root);

    if (reboot_required) {
        httpd_resp_sendstr(req, "Critical settings changed. Rebooting...");
        vTaskDelay(2000 / portTICK_PERIOD_MS);
        esp_restart();
    } else {
        httpd_resp_sendstr(req, "Settings saved.");
        void set_cfg_after_settings_change(void);
        set_cfg_after_settings_change();
    }

    return ESP_OK;
}

void set_cfg_after_settings_change(void)
{
    ESP_LOGI(TAG, "Configuration updated. Applying changes without reboot...");
    // TODO: Implement dynamic configuration update logic here
}

esp_err_t get_credentials_handler(httpd_req_t *req) {
    if (check_auth(req) != ESP_OK) return ESP_OK;
    cJSON *root = cJSON_CreateObject();
    char *val = NULL;

    // --- Port Configs ---
    if (storage_alloc_and_read(PORT_A_CFG_KEY, &val) == ESP_OK && val) {
        if(strcmp(val, "1") == 0) {
            cJSON_AddStringToObject(root, "pacfg", "0");
        } else {
            cJSON_AddStringToObject(root, "pacfg", val);
        }
        free(val); val = NULL;
    } else {
        cJSON_AddStringToObject(root, "pacfg", "0"); // Default
    }

    if (storage_alloc_and_read(PORT_B_CFG_KEY, &val) == ESP_OK && val) {
        cJSON_AddStringToObject(root, "pbcfg", val);
        free(val); val = NULL;
    } else {
        cJSON_AddStringToObject(root, "pbcfg", "0");
    }

    if (storage_alloc_and_read(PORT_C_CFG_KEY, &val) == ESP_OK && val) {
        cJSON_AddStringToObject(root, "pccfg", val);
        free(val); val = NULL;
    } else {
        cJSON_AddStringToObject(root, "pccfg", "0");
    }

    if (storage_alloc_and_read(PORT_D_CFG_KEY, &val) == ESP_OK && val) {
        cJSON_AddStringToObject(root, "pdcfg", val);
        free(val); val = NULL;
    } else {
        cJSON_AddStringToObject(root, "pdcfg", "0");
    }

    // --- Other Settings ---
    if (storage_alloc_and_read(TARGET_VOLTAGE_KEY, &val) == ESP_OK && val) {
        cJSON_AddStringToObject(root, "targetVoltage", val);
        free(val); val = NULL;
    } else {
        cJSON_AddStringToObject(root, "targetVoltage", "0");
    }

    if (storage_alloc_and_read(SW_MCU_KEY, &val) == ESP_OK && val) {
        cJSON_AddStringToObject(root, "swMcu", val);
        free(val); val = NULL;
    } else {
         cJSON_AddStringToObject(root, "swMcu", "0");
    }

    // --- WiFi ---
    // Station Mode SSID
    char *ssid_st = NULL;
    if (storage_alloc_and_read(WIFI_SSID_KEY, &ssid_st) == ESP_OK && ssid_st) {
        cJSON_AddStringToObject(root, "ssid_st", ssid_st);
    } else {
        cJSON_AddStringToObject(root, "ssid_st", "");
    }

    // AP Mode SSID
    char *ssid_ap = NULL;
    if (storage_alloc_and_read(WIFI_AP_SSID_KEY, &ssid_ap) == ESP_OK && ssid_ap) {
        cJSON_AddStringToObject(root, "ssid_ap", ssid_ap);
    } else {
        cJSON_AddStringToObject(root, "ssid_ap", ""); // Or default WIFI_AP_SSSID if we want to show default
    }

    // Populate generic 'ssid' for backward compatibility / initial view
    if (g_app_params.mode == APP_MODE_STA) {
        cJSON_AddStringToObject(root, "ssid", ssid_st ? ssid_st : "");
    } else {
        // If AP custom ssid exists, use it, else default
        if (ssid_ap) {
            cJSON_AddStringToObject(root, "ssid", ssid_ap);
        } else {
            cJSON_AddStringToObject(root, "ssid", WIFI_AP_SSSID);
        }
    }

    if (ssid_st) free(ssid_st);
    if (ssid_ap) free(ssid_ap);
     // Don't send password back
    cJSON_AddStringToObject(root, "password", "");

    // Use runtime mode to reflect actual status
    if (g_app_params.mode == APP_MODE_STA) {
        cJSON_AddStringToObject(root, "wifiMode", "SM");
    } else {
        cJSON_AddStringToObject(root, "wifiMode", "AP");
    }
    // Note: We intentionally ignore the stored WIFI_MODE_KEY here to avoid
    // discrepancies when fallback logic forces AP mode but NVS still has "SM".

    if (storage_alloc_and_read(OTA_URL_KEY, &val) == ESP_OK && val) {
        cJSON_AddStringToObject(root, "otaUrl", val);
        free(val); val = NULL;
    } else {
         cJSON_AddStringToObject(root, "otaUrl", "");
    }

    // Fixed values: MCU interface is always GPIO, UART always Web Terminal
    cJSON_AddStringToObject(root, "mcuInterface", "GPIO");
    cJSON_AddStringToObject(root, "uartPortSel", "1");

    // --- UART Settings ---
    if (storage_alloc_and_read(UART_BAUD_KEY, &val) == ESP_OK && val) {
        cJSON_AddStringToObject(root, "uartBaud", val);
        free(val); val = NULL;
    } else {
        cJSON_AddStringToObject(root, "uartBaud", "115200");
    }

    if (storage_alloc_and_read(UART_DATA_BITS_KEY, &val) == ESP_OK && val) {
        cJSON_AddStringToObject(root, "uartDataBits", val);
        free(val); val = NULL;
    } else {
        cJSON_AddStringToObject(root, "uartDataBits", "8");
    }

    if (storage_alloc_and_read(UART_STOP_BITS_KEY, &val) == ESP_OK && val) {
        cJSON_AddStringToObject(root, "uartStopBits", val);
        free(val); val = NULL;
    } else {
        cJSON_AddStringToObject(root, "uartStopBits", "1");
    }

    if (storage_alloc_and_read(UART_PARITY_KEY, &val) == ESP_OK && val) {
        cJSON_AddStringToObject(root, "uartParity", val);
        free(val); val = NULL;
    } else {
        cJSON_AddStringToObject(root, "uartParity", "n"); // 'n' for None
    }

    // Fixed value: USB DAP/COM is always disabled
    cJSON_AddBoolToObject(root, "disableUsbDapCom", true);

    const char *sys_info = cJSON_PrintUnformatted(root);
    httpd_resp_sendstr(req, sys_info);

    free((void *)sys_info);
    cJSON_Delete(root);

    return ESP_OK;
}


static void get_filename_from_path(const char *path, char *filename, size_t filename_len)
{
    const char *last_slash = strrchr(path, '/');
    const char *src = last_slash ? last_slash + 1 : path;
    strncpy(filename, src, filename_len - 1);
    filename[filename_len - 1] = '\0';
}

static esp_err_t file_upload_handler(httpd_req_t *req) {
    if (check_auth(req) != ESP_OK) return ESP_OK;

    struct stat file_stat;
    char filepath[128] = CFG_FILE_PATH;
    size_t prefix_len = strlen(CFG_FILE_PATH);

    get_filename_from_path(req->uri, filepath + prefix_len, sizeof(filepath) - prefix_len);

    if (stat(filepath, &file_stat) == 0) {
        ESP_LOGE(TAG, "File already exists : %s", filepath);
        /* Respond with 400 Bad Request */
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "File already exists!");
        return ESP_FAIL;
    }

    // Open the file for writing
    FILE *fp = fopen(filepath, "w");
    if (fp == NULL) {
        ESP_LOGE(TAG, "Failed to open file for writing");
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to open file");
        return ESP_FAIL;
    }

    char buf[1024];
    size_t remaining = req->content_len;
    const uint8_t poll_period = 10; /* ms */
    uint16_t try_count = 1000 / poll_period; /* 1 second timeout */

    while (try_count) {

        ESP_LOGI(TAG, "Remaining size : %d", remaining);

        // Read a chunk of data from the request
        int received = httpd_req_recv(req, buf, MIN(remaining, sizeof(buf)));
        if (received <= 0) {
            if (received == HTTPD_SOCK_ERR_TIMEOUT) {
                /* Retry if timeout occurred */
                continue;
            }

            /* In case of unrecoverable error, close and delete the unfinished file*/
            fclose(fp);
            unlink(filepath);

            ESP_LOGE(TAG, "File reception failed!");
            /* Respond with 500 Internal Server Error */
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to receive file");
            return ESP_FAIL;
        }

        /* Write buffer content to file on storage */
        if (received != fwrite(buf, 1, received, fp)) {
            /* Couldn't write everything to file! Storage may be full? */
            fclose(fp);
            unlink(filepath);

            ESP_LOGE(TAG, "File write failed!");
            /* Respond with 500 Internal Server Error */
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to write file to storage");
            return ESP_FAIL;
        }

        /* Keep track of remaining size of the file left to be uploaded */
        remaining -= received;
        if (!remaining) {
            break;
        }

        /* If still there is remaining data, something might be wrong. */
        try_count--;
        vTaskDelay(pdMS_TO_TICKS(poll_period));
    }

    // Close the file
    fclose(fp);
    ESP_LOGI(TAG, "File received: %s", filepath);

    storage_update_target_struct();

    // Send the response indicating success
    httpd_resp_sendstr(req, "File uploaded successfully");

    return ESP_OK;
}

static esp_err_t file_delete_handler(httpd_req_t *req) {
    if (check_auth(req) != ESP_OK) return ESP_OK;
    char filepath[128] = {0};
    char filename[64] = {0};

    get_filename_from_path(req->uri, filename, sizeof(filename));

    snprintf(filepath, sizeof(filepath), "%s%s", CFG_FILE_PATH, filename);

    ESP_LOGI(TAG, "filepath:%s", filepath);

    struct stat file_stat;

    if (stat(filepath, &file_stat) != 0) {
        ESP_LOGE(TAG, "File not exists : %s", filepath);
        /* Respond with 400 Bad Request */
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "File not exists!");
        return ESP_FAIL;
    }

    unlink(filepath);

    storage_update_target_struct();

    // Send the response indicating success
    httpd_resp_sendstr(req, "File deleted successfully");

    return ESP_OK;
}
#if 0
static esp_err_t logic_analyzer_data_handler(httpd_req_t *req) {
    if (check_auth(req) != ESP_OK) return ESP_OK;
    char query_str[64];
    if (httpd_req_get_url_query_str(req, query_str, sizeof(query_str)) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get query string");
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing query parameters");
        return ESP_FAIL;
    }

    int num_channels = 0;
    int num_samples = 0;
    
    char param[32];
    if (httpd_query_key_value(query_str, "channels", param, sizeof(param)) == ESP_OK) {
        num_channels = atoi(param);
    }
    if (httpd_query_key_value(query_str, "samples", param, sizeof(param)) == ESP_OK) {
        num_samples = atoi(param);
    }

    if (num_channels <= 0 || num_samples <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid number of channels or samples");
        return ESP_FAIL;
    }

    // Create a JSON object to hold the data
    cJSON *root = cJSON_CreateObject();
    cJSON *data_array = cJSON_CreateArray();

    // Call the data acquisition function (currently generates random data)
    // NOTE: To get real data, replace this function call with your actual data acquisition function.
    get_random_logic_analyzer_data(num_channels, num_samples, data_array);

    cJSON_AddItemToObject(root, "data", data_array);
    
    char *json_string = cJSON_PrintUnformatted(root);
    if (json_string == NULL) {
        ESP_LOGE(TAG, "Failed to create JSON string");
        cJSON_Delete(root);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to create JSON response");
        return ESP_FAIL;
    }

    httpd_resp_set_type(req, "application/json");
    esp_err_t err = httpd_resp_send(req, json_string, HTTPD_RESP_USE_STRLEN);
    
    free(json_string);
    cJSON_Delete(root);

    return err;
}
#endif

static esp_err_t log_error_handler(httpd_req_t *req)
{
    char json_buf[128];
    cJSON *root = set_handler_start(req, json_buf, sizeof(json_buf));
    if (!root) {
        return ESP_FAIL;
    }

    cJSON *message = cJSON_GetObjectItemCaseSensitive(root, "message");
    if (cJSON_IsString(message) && (message->valuestring != NULL)) {
        ESP_LOGE(TAG, "Client-side Error: %s", message->valuestring);
    } else {
        ESP_LOGE(TAG, "Client sent an unreadable error message.");
    }
    
    cJSON_Delete(root);
    
    // Respond to the client to confirm receipt
    httpd_resp_sendstr(req, "Error logged successfully.");

    return ESP_OK;
}

static esp_err_t la_start_capture_handler(httpd_req_t *req) {
    if (check_auth(req) != ESP_OK) return ESP_OK;
    ESP_LOGI(TAG, "=== la_start_capture_handler() called ===");
    
    // A C-style string buffer to hold the JSON response
    char resp_buf[128];

    // Set flags and call start_capture()
    gbl_capture_started = true;
    gbl_triggered_flag = false;
    gbl_all_captured_flag = false;
    
    ESP_LOGI(TAG, "la_start_capture_handler: Calling start_capture(false)...");
    extern void start_capture(bool without_trigger);
    start_capture(false);
    ESP_LOGI(TAG, "la_start_capture_handler: start_capture() completed");
    
    // Format the JSON string
    // We explicitly return "true" for capture_started because we just called start_capture()
    // and we don't want to rely on gbl_capture_started which might be cleared by the 
    // background thread (race condition).
    snprintf(resp_buf, sizeof(resp_buf), "{\"capture_started\": true, \"triggered\": %s, \"all_captured\": %s}",
             gbl_triggered_flag ? "true" : "false",
             gbl_all_captured_flag? "true" : "false");

    // Set the content type header
    httpd_resp_set_type(req, "application/json");

    // Send the response
    httpd_resp_send(req, resp_buf, HTTPD_RESP_USE_STRLEN);

    ESP_LOGI(TAG, "la_start_capture_handler: Response sent: '%s'", resp_buf);
    return ESP_OK;
}

// ==============================================================================
// TEST AUTOMATION LOGIC (Stubs/Implementation)
// ==============================================================================

static char g_current_test_name[32] = {0};
static char g_test_status[16] = "idle"; // idle, running, done
static char g_test_result_json[512] = "{}";
static bool g_test_is_running = false;

// Stub functions simulating hardware tests
void run_test_input(void) {
    ESP_LOGI(TAG, "Running test_input...");
    vTaskDelay(pdMS_TO_TICKS(1000)); // Simulate test duration
    snprintf(g_test_result_json, sizeof(g_test_result_json), "{\"test\": \"test_input\", \"result\": \"pass\", \"details\": \"Input pins verified\"}");
}

void run_test_output(void) {
    ESP_LOGI(TAG, "Running test_output...");
    vTaskDelay(pdMS_TO_TICKS(1000));
    snprintf(g_test_result_json, sizeof(g_test_result_json), "{\"test\": \"test_output\", \"result\": \"pass\", \"details\": \"Output pins toggled\"}");
}

void run_test_ad(void) {
    ESP_LOGI(TAG, "Running test_ad...");
    vTaskDelay(pdMS_TO_TICKS(1500));
    snprintf(g_test_result_json, sizeof(g_test_result_json), "{\"test\": \"test_ad\", \"result\": \"pass\", \"details\": \"ADC values within range\"}");
}

void run_test_iovoltage(void) {
    ESP_LOGI(TAG, "Running test_iovoltage...");
    vTaskDelay(pdMS_TO_TICKS(500));
    snprintf(g_test_result_json, sizeof(g_test_result_json), "{\"test\": \"test_iovoltage\", \"result\": \"pass\", \"voltage\": \"3.3V\"}");
}

void run_test_uart(void) {
    ESP_LOGI(TAG, "Running test_uart...");
    vTaskDelay(pdMS_TO_TICKS(2000));
    snprintf(g_test_result_json, sizeof(g_test_result_json), "{\"test\": \"test_uart\", \"result\": \"pass\", \"details\": \"Loopback successful\"}");
}

// Task that executes the test
void test_runner_task(void *pvParameters) {
    char *test_name = (char *)pvParameters;

    ESP_LOGI(TAG, "Test runner started for: %s", test_name);
    snprintf(g_test_status, sizeof(g_test_status), "running");

    if (strcmp(test_name, "test_input") == 0) run_test_input();
    else if (strcmp(test_name, "test_output") == 0) run_test_output();
    else if (strcmp(test_name, "test_ad") == 0) run_test_ad();
    else if (strcmp(test_name, "test_iovoltage") == 0) run_test_iovoltage();
    else if (strcmp(test_name, "test_uart") == 0) run_test_uart();
    else {
        ESP_LOGE(TAG, "Unknown test type: %s", test_name);
        snprintf(g_test_result_json, sizeof(g_test_result_json), "{\"test\": \"%s\", \"result\": \"error\", \"details\": \"Unknown test type\"}", test_name);
    }

    snprintf(g_test_status, sizeof(g_test_status), "done");
    g_test_is_running = false;
    free(test_name); // Free the allocated string
    vTaskDelete(NULL);
}

// Handler: /test/start
static esp_err_t test_start_handler(httpd_req_t *req) {
    ESP_LOGI(TAG, "test_start_handler called");
    //if (check_auth(req) != ESP_OK) {
    //    ESP_LOGW(TAG, "check_auth(req) failed");
    //    //return ESP_OK;
    //}

    if (g_test_is_running) {
        ESP_LOGW(TAG, "test_start_handler: Test already running");
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "A test is already running");
        return ESP_FAIL;
    }

    char json_buf[128];
    cJSON *root = set_handler_start(req, json_buf, sizeof(json_buf));
    if (!root) return ESP_FAIL;

    cJSON *test_type_item = cJSON_GetObjectItem(root, "test_type");
    if (!cJSON_IsString(test_type_item) || (test_type_item->valuestring == NULL)) {
        cJSON_Delete(root);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing or invalid 'test_type'");
        return ESP_FAIL;
    }

    // Allocate memory for the test name to pass to the task
    char *test_name_param = strdup(test_type_item->valuestring);
    snprintf(g_current_test_name, sizeof(g_current_test_name), "%s", test_type_item->valuestring);
    cJSON_Delete(root);

    ESP_LOGI(TAG, "Starting test: %s", g_current_test_name);

    g_test_is_running = true;
    xTaskCreate(test_runner_task, "test_runner", 4096, test_name_param, 5, NULL);

    httpd_resp_sendstr(req, "Test started");
    return ESP_OK;
}

// Handler: /test/status
static esp_err_t test_status_handler(httpd_req_t *req) {
    //if (check_auth(req) != ESP_OK) return ESP_OK;

    char resp[128];
    snprintf(resp, sizeof(resp), "{\"status\": \"%s\", \"test\": \"%s\"}", g_test_status, g_current_test_name);

    ESP_LOGI(TAG, "test_status_handler: %s", resp);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, resp, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

// Handler: /test/result
static esp_err_t test_result_handler(httpd_req_t *req) {
    //if (check_auth(req) != ESP_OK) return ESP_OK;

    ESP_LOGI(TAG, "test_result_handler: %s", g_test_result_json);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, g_test_result_json, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

static esp_err_t la_status_handler(httpd_req_t *req) {
    if (check_auth(req) != ESP_OK) return ESP_OK;
    // Call read_capture_status() to get fresh status
    read_capture_status();
    
    // A C-style string buffer to hold the JSON response
    char resp_buf[128]; 
    
    uint32_t trigger_pos = 128 * gbl_trigger_position / 100;
    if (trigger_pos > 127) {
        trigger_pos = 127;
    }
    trigger_pos = 127 - trigger_pos; // calculate from beginning
    uint32_t trigger_pos_in_samples = 64 * 1024 - trigger_pos * 512 - 14;
    
    // Format the JSON string
    snprintf(resp_buf, sizeof(resp_buf), "{\"triggered\": %s, \"all_captured\": %s, \"wr_addr_stop_position\":%" PRIu32 ", \"trigger_position\":%" PRIu32 "}",
             gbl_triggered_flag ? "true" : "false",
             gbl_all_captured_flag? "true" : "false",
             gbl_wr_addr_stop_position,
             trigger_pos_in_samples);
    
    ESP_LOGI(TAG, "la_status_handler: triggered=%s wr_addr=%d",
             gbl_triggered_flag ? "true" : "false",
             gbl_wr_addr_stop_position);

    // Send the response
    httpd_resp_send(req, resp_buf, HTTPD_RESP_USE_STRLEN);
    
    return ESP_OK;
}

static esp_err_t la_instant_capture_handler(httpd_req_t *req) {
    if (check_auth(req) != ESP_OK) return ESP_OK;
    ESP_LOGI(TAG, "=== la_instant_capture_handler() called ===");
    
    // 1. Start capture immediately (force trigger)
    start_capture(true);
#if 1    
    // 2. Wait for capture to complete (polling loop)
    ESP_LOGI(TAG, "la_instant_capture_handler: Waiting for capture completion...");
    
    int poll_count = 0;
    while (1) {
        read_capture_status();
        
        if (gbl_triggered_flag) {
            ESP_LOGI(TAG, "la_instant_capture_handler: Capture complete after %d polls", poll_count);
            break;
        }
        
        poll_count++;
        vTaskDelay(pdMS_TO_TICKS(10));
        
        // Safety timeout (~2 seconds)
        if (poll_count > 200) {
            ESP_LOGE(TAG, "la_instant_capture_handler: Timeout waiting for capture");
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Capture timeout");
            return ESP_FAIL;
        }
    }
#endif
    // 3. Read and return data
    ESP_LOGI(TAG, "la_instant_capture_handler: Calling read_and_return_capture()...");
    extern void read_and_return_capture(void);
    read_and_return_capture();
    
    // 4. Send data to browser
    ESP_LOGI(TAG, "la_instant_capture_handler: Sending data...");

    int64_t t_start = esp_timer_get_time();
    esp_err_t ret = httpd_resp_send(req, (const char *)(psram_buffer + CAP_DATA_START), ICE_CAPTURE_BUFFER_SIZE - CAP_DATA_START);
    int64_t t_end = esp_timer_get_time();

    float duration = (float)(t_end - t_start) / 1000000.0f;
    float speed = 0;
    if (duration > 0.000001f) {
        // Megabits per second (Mbps)
        speed = (float)(ICE_CAPTURE_BUFFER_SIZE - CAP_DATA_START) * 8.0f / 1000000.0f / duration;
    }

    ESP_LOGI(TAG, "la_instant_capture_handler: Sent %d bytes in %.3f s. Speed: %.2f mbps",
             (int)(ICE_CAPTURE_BUFFER_SIZE - CAP_DATA_START), duration, speed);
    
    return ret;
}

static esp_err_t la_configure_handler(httpd_req_t *req) {
    if (check_auth(req) != ESP_OK) return ESP_OK;
    char json_buf[512];
    cJSON *root = set_handler_start(req, json_buf, sizeof(json_buf));
    if (!root) {
        return ESP_FAIL;
    }

    cJSON *sampleRate = cJSON_GetObjectItem(root, "sampleRate");
    cJSON *triggerPosition = cJSON_GetObjectItem(root, "triggerPosition");
    cJSON *triggerEnabled = cJSON_GetObjectItem(root, "triggerEnabled");
    cJSON *triggerModeOR = cJSON_GetObjectItem(root, "triggerModeOR");
    cJSON *captureInternalTestSignal = cJSON_GetObjectItem(root, "captureInternalTestSignal");

    if (sampleRate) {
        gbl_sample_rate = sampleRate->valueint;

        // Map sample rate to register value
        if (gbl_sample_rate >= 264000000) gbl_sample_rate_reg = 255;
        else if (gbl_sample_rate >= 132000000) gbl_sample_rate_reg = 0;
        else if (gbl_sample_rate >= 66000000) gbl_sample_rate_reg = 1;
        else if (gbl_sample_rate >= 33000000) gbl_sample_rate_reg = 2;
        else if (gbl_sample_rate >= 22000000) gbl_sample_rate_reg = 3;
        else if (gbl_sample_rate >= 16500000) gbl_sample_rate_reg = 4;
        else if (gbl_sample_rate >= 11000000) gbl_sample_rate_reg = 6;
        else if (gbl_sample_rate >= 6000000) gbl_sample_rate_reg = 11;
        else if (gbl_sample_rate >= 3000000) gbl_sample_rate_reg = 22;
        else if (gbl_sample_rate >= 2000000) gbl_sample_rate_reg = 33;
        else if (gbl_sample_rate >= 1000000) gbl_sample_rate_reg = 66;
        else if (gbl_sample_rate >= 500000) gbl_sample_rate_reg = 132;
        else gbl_sample_rate_reg = 254; // 0.26MHz

        ESP_LOGI(TAG, "LA Config: Sample Rate = %u, Reg = %u", gbl_sample_rate, gbl_sample_rate_reg);
    }
    if (triggerPosition) {
        gbl_trigger_position = triggerPosition->valueint;
        ESP_LOGI(TAG, "LA Config: Trigger Position = %u%%", gbl_trigger_position);
    }
    if (triggerEnabled) {
        gbl_trigger_enabled = cJSON_IsTrue(triggerEnabled);
        ESP_LOGI(TAG, "LA Config: Trigger Enabled = %s", gbl_trigger_enabled ? "true" : "false");
    }
    if (triggerModeOR) {
        gbl_trigger_mode_or = cJSON_IsTrue(triggerModeOR);
        ESP_LOGI(TAG, "LA Config: Trigger Mode OR = %s", gbl_trigger_mode_or ? "true" : "false");
    }
    if (captureInternalTestSignal) {
        gbl_capture_internal_test_signal = cJSON_IsTrue(captureInternalTestSignal);
        ESP_LOGI(TAG, "LA Config: Capture Internal Test Signal = %s", gbl_capture_internal_test_signal ? "ON" : "OFF");
        set_la_input_sel(gbl_capture_internal_test_signal);
    }

    cJSON *channels = cJSON_GetObjectItem(root, "channels");
    if (channels && cJSON_IsArray(channels)) {
        int array_size = cJSON_GetArraySize(channels);
        for (int i = 0; i < array_size && i < 16; i++) {
            cJSON *item = cJSON_GetArrayItem(channels, i);
            if (cJSON_IsString(item)) {
                if (strcmp(item->valuestring, "disabled") == 0) gbl_channel_triggers[i] = TRIGGER_DISABLED;
                else if (strcmp(item->valuestring, "rising") == 0) gbl_channel_triggers[i] = TRIGGER_RISING;
                else if (strcmp(item->valuestring, "falling") == 0) gbl_channel_triggers[i] = TRIGGER_FALLING;
                else if (strcmp(item->valuestring, "crossing") == 0) gbl_channel_triggers[i] = TRIGGER_CROSSING;
                else if (strcmp(item->valuestring, "high") == 0) gbl_channel_triggers[i] = TRIGGER_HIGH;
                else if (strcmp(item->valuestring, "low") == 0) gbl_channel_triggers[i] = TRIGGER_LOW;
                else gbl_channel_triggers[i] = TRIGGER_DISABLED;
                ESP_LOGI(TAG, "LA Config: CH%d Trigger = %s", i+1, item->valuestring);
            }
        }
    }

    cJSON_Delete(root);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"status\": \"ok\"}");

    return ESP_OK;
}

static esp_err_t la_get_settings_handler(httpd_req_t *req) {
    if (check_auth(req) != ESP_OK) return ESP_OK;

    const char *trigger_names[] = {"disabled", "rising", "falling", "crossing", "high", "low"};

    cJSON *root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "sampleRate",    gbl_sample_rate);
    cJSON_AddBoolToObject  (root, "triggerEnabled", gbl_trigger_enabled);
    cJSON_AddBoolToObject  (root, "triggerModeOR",  gbl_trigger_mode_or);
    cJSON_AddBoolToObject  (root, "captureInternalTestSignal", gbl_capture_internal_test_signal);
    cJSON_AddNumberToObject(root, "triggerPosition", gbl_trigger_position);

    cJSON *channels = cJSON_CreateArray();
    for (int i = 0; i < 16; i++) {
        int t = (int)gbl_channel_triggers[i];
        const char *name = (t >= 0 && t <= 5) ? trigger_names[t] : "disabled";
        cJSON_AddItemToArray(channels, cJSON_CreateString(name));
    }
    cJSON_AddItemToObject(root, "channels", channels);

    char *json_str = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json_str, HTTPD_RESP_USE_STRLEN);
    free(json_str);

    return ESP_OK;
}

static esp_err_t version_handler(httpd_req_t *req) {
    if (check_auth(req) != ESP_OK) return ESP_OK;

    const esp_app_desc_t *app = esp_app_get_description();

    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "project_name",          app->project_name);
    cJSON_AddStringToObject(root, "firmware_version",      app->version);
    cJSON_AddStringToObject(root, "hardware_version",      HW_VERSION);
    cJSON_AddStringToObject(root, "idf_version",           app->idf_ver);
    cJSON_AddStringToObject(root, "build_date",            app->date);
    cJSON_AddStringToObject(root, "build_time",            app->time);
    cJSON_AddStringToObject(root, "main_git_commit",       MAIN_GIT_COMMIT);
    cJSON_AddStringToObject(root, "blackmagic_git_commit", FIRMWARE_VERSION);

    char *json_str = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, json_str);
    free(json_str);

    return ESP_OK;
}

esp_err_t reset_to_factory_handler(httpd_req_t *req) {
    if (check_auth(req) != ESP_OK) return ESP_OK;

    ESP_LOGW(TAG, "Factory Reset Initialized by User");

    storage_erase_key(PORT_A_CFG_KEY);
    storage_erase_key(PORT_B_CFG_KEY);
    storage_erase_key(PORT_C_CFG_KEY);
    storage_erase_key(PORT_D_CFG_KEY);
    storage_erase_key(TARGET_VOLTAGE_KEY);
    storage_erase_key(SW_MCU_KEY);
    storage_erase_key(WIFI_MODE_KEY);
    storage_erase_key(WIFI_SSID_KEY);
    storage_erase_key(WIFI_PASS_KEY);
    storage_erase_key(WIFI_AP_SSID_KEY);
    storage_erase_key(WIFI_AP_PASS_KEY);
    storage_erase_key(OTA_URL_KEY);
    storage_erase_key(MCU_INTERFACE_KEY);
    
    // Also erase web credentials so it defaults to admin:admin
    storage_erase_key(WEB_USER_KEY);
    storage_erase_key(WEB_PASS_KEY);

    // Erase UART settings
    storage_erase_key(UART_BAUD_KEY);
    storage_erase_key(UART_DATA_BITS_KEY);
    storage_erase_key(UART_STOP_BITS_KEY);
    storage_erase_key(UART_PARITY_KEY);
    storage_erase_key(UART_PORT_SEL_KEY);
    storage_erase_key(DISABLE_USB_DAP_KEY);

    httpd_resp_sendstr(req, "Factory reset complete. Rebooting...");
    vTaskDelay(3000 / portTICK_PERIOD_MS);
    esp_restart();
    return ESP_OK;
}

httpd_uri_t uri_reset_to_factory = {
    .uri       = "/reset_to_factory",
    .method    = HTTP_POST,
    .handler   = reset_to_factory_handler,
    .user_ctx  = NULL
};

httpd_uri_t uri_ota_upload = {
    .uri      = "/ota_upload",
    .method   = HTTP_POST,
#ifndef FPGA_CFG_TEST 
    .handler  = ota_upload_handler,
#else
    .handler  = fpga_loader_handler,
#endif
    .user_ctx = NULL
};

httpd_uri_t uri_get_main_page = {
    .uri = "/",
    .method = HTTP_GET,
    .handler = website_handler,
    .user_ctx = NULL
};

httpd_uri_t uri_get_logo = {
    .uri = "/ez32.svg",
    .method = HTTP_GET,
    .handler = ez32_handler,
    .user_ctx = NULL
};

httpd_uri_t uri_get_favicon = {
    .uri = "/favicon.ico",
    .method = HTTP_GET,
    .handler = favicon_handler,
    .user_ctx = NULL
};

httpd_uri_t uri_set_credentials = {
    .uri = "/set_credentials",
    .method = HTTP_POST,
    .handler = set_credentials_handler,
    .user_ctx = NULL
};

httpd_uri_t uri_get_credentials = {
    .uri = "/get_credentials",
    .method = HTTP_GET,
    .handler = get_credentials_handler,
    .user_ctx = NULL
};


httpd_uri_t uri_file_upload = {
    .uri       = "/upload/*",
    .method    = HTTP_POST,
    .handler   = file_upload_handler,
    .user_ctx  = NULL
};

httpd_uri_t uri_file_delete = {
    .uri       = "/delete/*",
    .method    = HTTP_POST,
    .handler   = file_delete_handler,
    .user_ctx  = NULL
};

httpd_uri_t uri_logic_analyzer = {
    .uri = "/logic_analyzer.html",
    .method = HTTP_GET,
    .handler = logic_analyzer_handler,
    .user_ctx = NULL
};

httpd_uri_t uri_help = {
    .uri = "/help",
    .method = HTTP_GET,
    .handler = help_handler,
    .user_ctx = NULL
};
/*
httpd_uri_t uri_logic_analyzer_data = {
    .uri = "/logic_analyzer_data",
    .method = HTTP_GET,
    .handler = logic_analyzer_data_handler,
    .user_ctx = NULL
};
*/
httpd_uri_t uri_log_error = {
    .uri = "/log_error",
    .method = HTTP_POST,
    .handler = log_error_handler,
    .user_ctx = NULL
};

esp_err_t not_found_handler(httpd_req_t *req, httpd_err_code_t err_code)
{
    httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "The requested URI was not found on this server.");
    return ESP_OK;
}

httpd_uri_t uri_capture_data= {
    .uri = "/capture_data",
    .method = HTTP_GET,
    .handler = capture_data_handler,
    .user_ctx = NULL
};

httpd_uri_t uri_la_status = {
    .uri      = "/la_status",
    .method   = HTTP_GET,
    .handler  = la_status_handler,
    .user_ctx = NULL
};

httpd_uri_t uri_la_instant_capture = {
    .uri      = "/instant_capture",
    .method   = HTTP_GET,
    .handler  = la_instant_capture_handler,
    .user_ctx = NULL
};

httpd_uri_t uri_la_start_capture = {
    .uri      = "/la_start_capture",
    .method   = HTTP_GET,
    .handler  = la_start_capture_handler,
    .user_ctx = NULL
};

httpd_uri_t uri_la_configure = {
    .uri      = "/la_configure",
    .method   = HTTP_POST,
    .handler  = la_configure_handler,
    .user_ctx = NULL
};

httpd_uri_t uri_la_get_settings = {
    .uri      = "/la_get_settings",
    .method   = HTTP_GET,
    .handler  = la_get_settings_handler,
    .user_ctx = NULL
};

httpd_uri_t uri_version = {
    .uri      = "/api/version",
    .method   = HTTP_GET,
    .handler  = version_handler,
    .user_ctx = NULL
};

httpd_uri_t uri_test_start = {
    .uri      = "/test/start",
    .method   = HTTP_POST,
    .handler  = test_start_handler,
    .user_ctx = NULL
};

httpd_uri_t uri_test_status = {
    .uri      = "/test/status",
    .method   = HTTP_GET,
    .handler  = test_status_handler,
    .user_ctx = NULL
};

httpd_uri_t uri_test_result = {
    .uri      = "/test/result",
    .method   = HTTP_GET,
    .handler  = test_result_handler,
    .user_ctx = NULL
};

#include "esp_https_server.h"

extern const unsigned char cacert_pem_start[] asm("_binary_cacert_pem_start");
extern const unsigned char cacert_pem_end[]   asm("_binary_cacert_pem_end");
extern const unsigned char prvtkey_pem_start[] asm("_binary_prvtkey_pem_start");
extern const unsigned char prvtkey_pem_end[]   asm("_binary_prvtkey_pem_end");

esp_err_t web_server_start(httpd_handle_t *http_handle) {
    httpd_ssl_config_t config = HTTPD_SSL_CONFIG_DEFAULT();

    // Note: mbedtls requires PEM to be null-terminated. Embedded files might not be.
    // We allocate a buffer, copy, and adhere to requirements.
    
    size_t cacert_len = cacert_pem_end - cacert_pem_start;
    size_t prvtkey_len = prvtkey_pem_end - prvtkey_pem_start;

    // Allocate buffers with space for \0
    uint8_t *cacert_buf = calloc(1, cacert_len + 1);
    uint8_t *prvtkey_buf = calloc(1, prvtkey_len + 1);

    if (!cacert_buf || !prvtkey_buf) {
        ESP_LOGE(TAG, "Failed to allocate memory for SSL certs");
        free(cacert_buf);
        free(prvtkey_buf);
        return ESP_ERR_NO_MEM;
    }

    memcpy(cacert_buf, cacert_pem_start, cacert_len);
    cacert_buf[cacert_len] = '\0';

    memcpy(prvtkey_buf, prvtkey_pem_start, prvtkey_len);
    prvtkey_buf[prvtkey_len] = '\0';

    config.servercert = cacert_buf;
    config.servercert_len = cacert_len + 1; // Include null terminator
    config.prvtkey_pem = prvtkey_buf;
    config.prvtkey_len = prvtkey_len + 1; // Include null terminator

    config.httpd.max_uri_handlers = 30;
    config.httpd.stack_size = 10240; // Increased stack for SSL operations

    ESP_LOGI(TAG, "Starting HTTPS Server on port: '%d'", config.httpd.server_port);
    esp_err_t ret = httpd_ssl_start(http_handle, &config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start web server! Error: %d", ret);
    }

    httpd_register_uri_handler(*http_handle, &uri_get_main_page);
    httpd_register_uri_handler(*http_handle, &uri_get_logo);
    httpd_register_uri_handler(*http_handle, &uri_get_favicon);
    httpd_register_uri_handler(*http_handle, &uri_set_credentials);
    httpd_register_uri_handler(*http_handle, &uri_get_credentials);
    httpd_register_uri_handler(*http_handle, &uri_file_upload);
    httpd_register_uri_handler(*http_handle, &uri_file_delete);
    httpd_register_uri_handler(*http_handle, &uri_logic_analyzer);
    httpd_register_uri_handler(*http_handle, &uri_help);
    //httpd_register_uri_handler(*http_handle, &uri_logic_analyzer_data);
    httpd_register_uri_handler(*http_handle, &uri_log_error);
    httpd_register_uri_handler(*http_handle, &uri_ota_upload);
    httpd_register_uri_handler(*http_handle, &uri_reset_to_factory);

    httpd_register_err_handler(*http_handle, HTTPD_404_NOT_FOUND, not_found_handler);
    ESP_ERROR_CHECK(uart_websocket_add_handlers(*http_handle));

    httpd_register_uri_handler(*http_handle, &uri_la_start_capture);
    httpd_register_uri_handler(*http_handle, &uri_la_status);
    httpd_register_uri_handler(*http_handle, &uri_la_instant_capture);
    httpd_register_uri_handler(*http_handle, &uri_capture_data);
    httpd_register_uri_handler(*http_handle, &uri_la_configure);
    httpd_register_uri_handler(*http_handle, &uri_la_get_settings);
    httpd_register_uri_handler(*http_handle, &uri_version);

    // Test automation endpoints
    httpd_register_uri_handler(*http_handle, &uri_test_start);
    httpd_register_uri_handler(*http_handle, &uri_test_status);
    httpd_register_uri_handler(*http_handle, &uri_test_result);
    
    // We cannot free the buffers here if httpd_ssl_start uses them by reference!
    // Documentation for httpd_ssl_start isn't explicit if it copies.
    // However, usually mbedtls parses it into its own context.
    // BUT httpd server keeps the config struct?
    // Actually, checking standard implementation: httpd_ssl_start creates a session and usually the context persists.
    // If we free it, and it accessed it later (e.g. for new connections/re-handshake), crash.
    // Given this is the global web server, leaking 2-3KB once is acceptable vs crashing.
    // So we do NOT free them.
    
    return ret;
}
