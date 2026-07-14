#pragma once

#include <stdint.h>
#include "esp_log.h"

#ifdef __cplusplus
extern "C" {
#endif

// Functions that take a string (for web server handlers)
const char* get_port_a_description(const char* val);
const char* get_port_b_description(const char* val);
const char* get_port_c_description(const char* val);
const char* get_port_d_description(const char* val);
const char* get_target_voltage_description(const char* val);
const char* get_sw_mcu_description(const char* val);
const char* get_wifi_mode_description(const char* val);

// Functions that take an integer (for logging in main.c)
const char* get_port_a_description_int(uint8_t val);
const char* get_port_b_description_int(uint8_t val);
const char* get_port_c_description_int(uint8_t val);
const char* get_port_d_description_int(uint8_t val);

#ifdef __cplusplus
}
#endif