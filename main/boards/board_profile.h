#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "driver/gpio.h"
#include "../../components/platform_include/board_profile.h"

typedef struct {
    const char *name;
    bool has_fpga;
    bool has_lcd;
    bool has_target_vio_pwm;
    bool has_logic_analyzer;
    bool has_xvc;
    bool has_secondary_button;
    int button_boot_pin;
    int button_secondary_pin;
} ael_board_profile_t;

const ael_board_profile_t *ael_board_profile_get(void);
uint64_t ael_gpio_mask_if_valid(int pin);
bool ael_board_has_valid_gpio(gpio_num_t pin);
