#include <string.h>
#include <esp_ota_ops.h>
#include <esp_http_client.h>
#include <esp_https_ota.h>
#include <esp_wifi.h>
#include <esp_event.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"

#include "esp_log.h"
#include "esp_system.h"

#include "network_mngr.h"
#include "network_mngr_ota.h"

#define WIFI_STOP_TIMEOUT_MS         5000
#define WIFI_DEINIT_TIMEOUT_MS       5000

/* s_event_group, s_sta_netif, s_ap_netif and WiFi bit constants
 * are declared in network_mngr.h */

static const char *TAG_OTA = "network-mngr-ota";

/**
 * @brief Helper function to disconnect and de-initialize Wi-Fi.
 * This is crucial before switching Wi-Fi modes for OTA.
 *
 * @return ESP_OK on success, ESP_FAIL otherwise.
 */
static esp_err_t network_mngr_disconnect_wifi(void)
{
    esp_err_t err;

    // Unregister event handlers to prevent unexpected events during deinit
    network_mngr_unregister_wifi_handlers();

    err = esp_wifi_stop();
    if (err != ESP_OK && err != ESP_ERR_WIFI_NOT_INIT) { // Not_init is OK if already stopped
        ESP_LOGW(TAG_OTA, "esp_wifi_stop failed: %s", esp_err_to_name(err));
    }
    err = esp_wifi_deinit();
    if (err != ESP_OK && err != ESP_ERR_WIFI_NOT_INIT) {
        ESP_LOGE(TAG_OTA, "esp_wifi_deinit failed: %s", esp_err_to_name(err));
        return ESP_FAIL;
    }

    if (s_sta_netif) {
        esp_netif_destroy(s_sta_netif);
        s_sta_netif = NULL;
    }
    if (s_ap_netif) {
        esp_netif_destroy(s_ap_netif);
        s_ap_netif = NULL;
    }

    ESP_LOGI(TAG_OTA, "Wi-Fi stopped and de-initialized successfully.");
    return ESP_OK;
}

/**
 * @brief Performs the core OTA update process.
 *
 * @param ota_url The URL of the firmware image.
 * @return ESP_OK on successful update (device restarts), ESP_FAIL otherwise.
 */
static esp_err_t perform_ota_update(const char *ota_url)
{
    ESP_LOGI(TAG_OTA, "Starting OTA update from: %s", ota_url);

    esp_http_client_config_t http_config = {
        .url = ota_url,
        .timeout_ms = 5000,
        .keep_alive_enable = true,
        // For HTTPS:
        // .cert_pem = YOUR_SERVER_ROOT_CA_PEM, // If using custom CA
        // .skip_cert_common_name_check = true, // Use with caution!
    };

    esp_https_ota_config_t config = {
        .http_config = &http_config,
    };

    esp_err_t ret = esp_https_ota(&config);

    if (ret == ESP_OK) {
        ESP_LOGI(TAG_OTA, "OTA Update successful. Restarting...");
        vTaskDelay(pdMS_TO_TICKS(2000));
        esp_restart();
    } else {
        ESP_LOGE(TAG_OTA, "Firmware upgrade failed (%s)!", esp_err_to_name(ret));
        // Allow time for message to display before returning
        vTaskDelay(pdMS_TO_TICKS(3000));
    }
    return ret;
}

esp_err_t network_mngr_start_ota_sta(const char *ota_url)
{
    if (!ota_url || strlen(ota_url) == 0) {
        ESP_LOGE(TAG_OTA, "%s: OTA URL is null or empty!", __func__);
        return ESP_FAIL;
    }

    ESP_LOGI(TAG_OTA, "Initiating OTA in STA mode...");

    network_mngr_states_t current_state = network_mngr_state(pdMS_TO_TICKS(1000)); // Short wait
    if (current_state != NETWORK_MNGR_CONNECTED) {
        ESP_LOGE(TAG_OTA, "Device not connected to Wi-Fi in STA mode. Cannot perform OTA.");
        vTaskDelay(pdMS_TO_TICKS(3000));
        return ESP_FAIL;
    }

    return perform_ota_update(ota_url);
}

esp_err_t network_mngr_start_ota_ap_temp_sta(const char *ota_url, const char *temp_sta_ssid, const char *temp_sta_password)
{
    if (!ota_url || strlen(ota_url) == 0 ||
        !temp_sta_ssid || strlen(temp_sta_ssid) == 0 ||
        !temp_sta_password) // Password can be empty for open network
    {
        ESP_LOGE(TAG_OTA, "%s: Invalid input parameters for AP mode OTA!", __func__);
        vTaskDelay(pdMS_TO_TICKS(3000));
        return ESP_FAIL;
    }

    ESP_LOGI(TAG_OTA, "Initiating OTA from AP mode by temporarily switching to STA mode...");

    // 1. Disconnect and de-initialize current Wi-Fi (AP mode)
    if (network_mngr_disconnect_wifi() != ESP_OK) {
        ESP_LOGE(TAG_OTA, "Failed to disconnect/de-init current Wi-Fi for AP OTA.");
        return ESP_FAIL;
    }

    // 2. Initialize Wi-Fi in STA mode with temporary credentials
    ESP_LOGI(TAG_OTA, "Initializing STA with SSID: %s", temp_sta_ssid);
    if (network_mngr_init_sta(temp_sta_ssid, temp_sta_password) != ESP_OK) {
        ESP_LOGE(TAG_OTA, "Failed to initialize STA mode for OTA.");
        return ESP_FAIL;
    }

    // 3. Connect to the temporary STA network
    ESP_LOGI(TAG_OTA, "Connecting to temporary STA network for OTA...");
    if (network_mngr_connect_sta(5) != ESP_OK) { // Max 5 retries to connect
        ESP_LOGE(TAG_OTA, "Failed to connect to temporary STA network for OTA. OTA cancelled.");
        // Attempt to re-initialize original AP mode if possible, or just fail
        return ESP_FAIL;
    }
    ESP_LOGI(TAG_OTA, "Successfully connected to temporary STA network.");

    // 4. Perform the OTA update
    return perform_ota_update(ota_url);
}