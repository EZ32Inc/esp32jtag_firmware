#ifndef UART_WEBSOCKET_H_
#define UART_WEBSOCKET_H_
#include "esp32jtag_common.h"

#ifdef __cplusplus
extern "C" {
#endif

#define FILE_PATH_MAX       (ESP_VFS_PATH_MAX + 128)
#define SCRATCH_BUFSIZE     (10240)

#define UART_BUF_SIZE       (1024*4)

esp_err_t uart_websocket_add_handlers(httpd_handle_t server);
extern QueueHandle_t s_ws_client_queue;

#ifdef __cplusplus
}
#endif

#endif // UART_WEBSOCKET_H_
