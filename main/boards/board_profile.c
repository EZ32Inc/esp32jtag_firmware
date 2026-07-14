#include "boards/board_profile.h"

static const ael_board_profile_t g_board_profile = {
    .name = CONFIG_AEL_BOARD_NAME,
    .has_fpga = AEL_BOARD_HAS_FPGA,
    .has_lcd = AEL_BOARD_HAS_LCD,
    .has_target_vio_pwm = AEL_BOARD_HAS_TARGET_VIO_PWM,
    .has_logic_analyzer = AEL_BOARD_HAS_LOGIC_ANALYZER,
    .has_xvc = AEL_BOARD_HAS_XVC,
    .has_secondary_button = AEL_BOARD_HAS_SECONDARY_BUTTON,
    .button_boot_pin = AEL_BUTTON_BOOT_PIN,
    .button_secondary_pin = AEL_BUTTON_SECONDARY_PIN,
};

const ael_board_profile_t *ael_board_profile_get(void)
{
    return &g_board_profile;
}

uint64_t ael_gpio_mask_if_valid(int pin)
{
    if (pin < 0 || pin >= GPIO_NUM_MAX) {
        return 0;
    }
    return 1ULL << pin;
}

bool ael_board_has_valid_gpio(gpio_num_t pin)
{
    return pin >= 0 && pin < GPIO_NUM_MAX;
}
