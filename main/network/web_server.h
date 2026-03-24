#pragma once

#include "esp_log.h"
#include "esp_http_server.h"

esp_err_t web_server_start(httpd_handle_t *http_handle);

extern bool gbl_capture_internal_test_signal;
extern uint8_t gbl_sample_rate_reg;

esp_err_t get_credentials_handler(httpd_req_t *req);