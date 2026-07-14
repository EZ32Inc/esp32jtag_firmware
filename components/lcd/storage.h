#pragma once

#include "esp_err.h"

/* Network params */
#define WIFI_SSID_KEY               "ssid"
#define WIFI_PASS_KEY               "pass"

#define CFG_FILE_PATH               "/data/target/"

esp_err_t storage_init_filesystem(void);
esp_err_t storage_write(const char *key, const char *value, size_t len);
esp_err_t storage_read(const char *key, char *value, size_t len);
esp_err_t storage_erase_key(const char *key);
size_t storage_get_value_length(const char *key);
bool storage_is_key_exist(const char *key);
esp_err_t storage_erase_all(void);
esp_err_t storage_alloc_and_read(char *key, char **value);
esp_err_t storage_update_target_struct(void);
esp_err_t storage_update_rtos_struct(void);
