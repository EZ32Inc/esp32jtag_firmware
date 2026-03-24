#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t network_mngr_start_ota_sta(const char *ota_url);
esp_err_t network_mngr_start_ota_ap_temp_sta(const char *ota_url, const char *temp_sta_ssid, const char *temp_sta_password);

#ifdef __cplusplus
}
#endif