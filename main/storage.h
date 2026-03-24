#pragma once

#include "esp_err.h"

/* OpenOCD params */
#define OOCD_CFG_FILE_KEY           "file"
#define OOCD_RTOS_TYPE_KEY          "rtos"
#define OOCD_DUAL_CORE_KEY          "smp"
#define OOCD_FLASH_SUPPORT_KEY      "flash"
#define OOCD_INTERFACE_KEY          "interface"
#define OOCD_CMD_LINE_ARGS_KEY      "command"
#define OOCD_DBG_LEVEL_KEY          "debug"

/* Ports */
#define PORT_A_CFG_KEY "pa_cfg"
#define PORT_B_CFG_KEY "pb_cfg"
#define PORT_C_CFG_KEY "pc_cfg"
#define PORT_D_CFG_KEY "pd_cfg"

/* Network params */
#define WIFI_SSID_KEY               "ssid"
#define WIFI_PASS_KEY               "pass"
#define WIFI_AP_SSID_KEY            "ap_ssid"
#define WIFI_AP_PASS_KEY            "ap_pass"
#define TARGET_VOLTAGE_KEY          "target_voltage"
#define SW_MCU_KEY                  "sw_mcu"
#define WIFI_MODE_KEY               "wifi_mode"
#define MCU_INTERFACE_KEY           "mcu_if"
#define OTA_URL_KEY                 "ota_url_key"
#define WEB_USER_KEY                "web_user"
#define WEB_PASS_KEY                "web_pass"

/* UART Configuration */
#define UART_BAUD_KEY               "uart_baud"
#define UART_DATA_BITS_KEY          "uart_dbits"
#define UART_STOP_BITS_KEY          "uart_sbits"
#define UART_PARITY_KEY             "uart_parity"
#define UART_PORT_SEL_KEY           "uart_psel"
#define DISABLE_USB_DAP_KEY         "dis_usb_dap"

#define CFG_FILE_PATH               "/data/target/"

esp_err_t storage_init_filesystem(void);
esp_err_t storage_init_nvs(void);
esp_err_t storage_write(const char *key, const char *value, size_t len);
esp_err_t storage_read(const char *key, char *value, size_t len);
esp_err_t storage_erase_key(const char *key);
size_t storage_get_value_length(const char *key);
bool storage_is_key_exist(const char *key);
esp_err_t storage_erase_all(void);
esp_err_t storage_alloc_and_read(const char *key, char **value);
esp_err_t storage_update_target_struct(void);
esp_err_t storage_update_rtos_struct(void);
bool is_espressif_target(const char *cfg_file);
void storage_set_bool_key(const char *key, bool value);
void storage_get_bool_key(const char *key, bool *value);

typedef uint32_t storage_handle_t;
esp_err_t storage_open_session(storage_handle_t *handle);
esp_err_t storage_write_session(storage_handle_t handle, const char *key, const char *value, size_t len);
void storage_close_session(storage_handle_t handle);