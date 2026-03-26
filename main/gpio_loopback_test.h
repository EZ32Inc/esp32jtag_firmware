#pragma once

#include <stddef.h>
#include <stdbool.h>

bool gpio_loopback_test_is_supported(void);
void gpio_loopback_test_run_json(char *out_json, size_t out_json_size);
void gpio_targetin_detect_run_json(char *out_json, size_t out_json_size);
