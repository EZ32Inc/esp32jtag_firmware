#pragma once

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "esp_netif.h"

/* WiFi event-group bits shared between network_mngr.c and network_mngr_ota.c */
#define WIFI_CONNECTED_BIT      BIT0
#define WIFI_DISCONNECTED_BIT   BIT1
#define WIFI_STARTED_BIT        BIT2

/* Handles exposed for OTA module (avoids extern scattered in .c files) */
extern EventGroupHandle_t s_event_group;
extern esp_netif_t *s_sta_netif;
extern esp_netif_t *s_ap_netif;

typedef enum {
    NETWORK_MNGR_CONNECTED,
    NETWORK_MNGR_DISCONNECTED,
    NETWORK_MNGR_STATUS_NOT_CHANGED,
    NETWORK_MNGR_ERROR
} network_mngr_states_t;

esp_err_t network_mngr_init(void);
esp_err_t network_mngr_init_ap(const char *ssid, const char *pass);
esp_err_t network_mngr_connect_ap(unsigned int max_retry);
esp_err_t network_mngr_init_sta(const char *ssid, const char *pass);
esp_err_t network_mngr_connect_sta(unsigned int max_retry);
esp_err_t network_mngr_init_prov(const char *ssid, const char *pass, const void *http_handle);
esp_err_t network_mngr_connect_prov(unsigned int max_retry);
network_mngr_states_t network_mngr_state(unsigned int timeout);
esp_err_t network_mngr_get_sta_credentials(char **ssid, char **pass);
esp_err_t network_mngr_get_sta_ip(char **ip);
esp_err_t network_mngr_get_ap_ip(char **ip);

void network_mngr_unregister_wifi_handlers(void);