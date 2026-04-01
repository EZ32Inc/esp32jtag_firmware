#include "gpio_loopback_test.h"

#include <stdio.h>
#include <inttypes.h>

#include "driver/gpio.h"
#include "esp_check.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "esp_timer.h"

#include "boards/board_profile.h"

static const char *TAG = "gpio-loopback";

#if CONFIG_AEL_BOARD_ESP32S3_DEVKIT
#define AEL_GPIO_TEST_SUPPORTED 1
#define AEL_GPIO_TEST_SRESET_OUT GPIO_NUM_16
#define AEL_GPIO_TEST_TARGETIN   GPIO_NUM_15
#define AEL_GPIO_TEST_SRESET_OUT_MASK (1ULL << AEL_GPIO_TEST_SRESET_OUT)
#define AEL_GPIO_TEST_TARGETIN_MASK   (1ULL << AEL_GPIO_TEST_TARGETIN)
#else
#define AEL_GPIO_TEST_SUPPORTED 0
#define AEL_GPIO_TEST_SRESET_OUT GPIO_NUM_NC
#define AEL_GPIO_TEST_TARGETIN   GPIO_NUM_NC
#define AEL_GPIO_TEST_SRESET_OUT_MASK 0ULL
#define AEL_GPIO_TEST_TARGETIN_MASK   0ULL
#endif

static esp_err_t configure_test_pins(void)
{
    if (!AEL_GPIO_TEST_SUPPORTED) {
        return ESP_ERR_NOT_SUPPORTED;
    }

    gpio_config_t out_conf = {
        .pin_bit_mask = AEL_GPIO_TEST_SRESET_OUT_MASK,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&out_conf), TAG, "configure output");

    gpio_config_t in_conf = {
        .pin_bit_mask = AEL_GPIO_TEST_TARGETIN_MASK,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&in_conf), TAG, "configure input");

    ESP_RETURN_ON_ERROR(gpio_set_level(AEL_GPIO_TEST_SRESET_OUT, 1), TAG, "set idle high");
    esp_rom_delay_us(2000);
    return ESP_OK;
}

static bool wait_for_input_level(int expected_level, uint32_t timeout_us, uint32_t sample_step_us)
{
    int64_t deadline = esp_timer_get_time() + timeout_us;
    while (esp_timer_get_time() < deadline) {
        if (gpio_get_level(AEL_GPIO_TEST_TARGETIN) == expected_level) {
            return true;
        }
        esp_rom_delay_us(sample_step_us);
    }
    return gpio_get_level(AEL_GPIO_TEST_TARGETIN) == expected_level;
}

static bool run_pulse_check(uint32_t pulse_ms)
{
    if (gpio_set_level(AEL_GPIO_TEST_SRESET_OUT, 0) != ESP_OK) {
        return false;
    }
    bool saw_low = wait_for_input_level(0, 10000, 50);
    esp_rom_delay_us(pulse_ms * 1000U);
    bool low_still_present = (gpio_get_level(AEL_GPIO_TEST_TARGETIN) == 0);
    gpio_set_level(AEL_GPIO_TEST_SRESET_OUT, 1);
    bool returned_high = wait_for_input_level(1, 10000, 50);
    return saw_low && low_still_present && returned_high;
}

static bool run_square_wave_check(uint32_t freq_hz, uint32_t cycles, uint32_t *edges_seen_out)
{
    const uint32_t half_period_us = 1000000U / (freq_hz * 2U);
    const uint32_t settle_us = half_period_us > 50U ? 50U : (half_period_us ? half_period_us : 1U);
    uint32_t observed_edges = 0;
    int prev_level = gpio_get_level(AEL_GPIO_TEST_TARGETIN);

    for (uint32_t i = 0; i < cycles * 2U; ++i) {
        const int next_level = (i & 1U) ? 0 : 1;
        if (gpio_set_level(AEL_GPIO_TEST_SRESET_OUT, next_level) != ESP_OK) {
            return false;
        }
        esp_rom_delay_us(settle_us);
        int sampled = gpio_get_level(AEL_GPIO_TEST_TARGETIN);
        if (sampled != next_level) {
            ESP_LOGW(TAG, "freq=%" PRIu32 " mismatch: expected=%d sampled=%d step=%" PRIu32,
                     freq_hz, next_level, sampled, i);
            gpio_set_level(AEL_GPIO_TEST_SRESET_OUT, 1);
            return false;
        }
        if (sampled != prev_level) {
            observed_edges++;
            prev_level = sampled;
        }
        if (half_period_us > settle_us) {
            esp_rom_delay_us(half_period_us - settle_us);
        }
    }

    gpio_set_level(AEL_GPIO_TEST_SRESET_OUT, 1);
    wait_for_input_level(1, 10000, 50);

    if (edges_seen_out) {
        *edges_seen_out = observed_edges;
    }
    return observed_edges >= (cycles * 2U - 1U);
}

static esp_err_t configure_targetin_only(void)
{
    if (!AEL_GPIO_TEST_SUPPORTED) {
        return ESP_ERR_NOT_SUPPORTED;
    }

    gpio_config_t in_conf = {
        .pin_bit_mask = AEL_GPIO_TEST_TARGETIN_MASK,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&in_conf), TAG, "configure targetin input");
    return ESP_OK;
}

static void sample_targetin(uint32_t duration_ms, uint32_t sample_step_us,
                            uint32_t *samples_out, uint32_t *high_out,
                            uint32_t *low_out, uint32_t *transitions_out,
                            uint32_t *estimated_hz_out, const char **state_out)
{
    uint32_t samples = 0;
    uint32_t high = 0;
    uint32_t low = 0;
    uint32_t transitions = 0;
    const int64_t start_us = esp_timer_get_time();
    const int64_t end_us = start_us + ((int64_t)duration_ms * 1000LL);
    int prev = gpio_get_level(AEL_GPIO_TEST_TARGETIN);

    while (esp_timer_get_time() < end_us) {
        int level = gpio_get_level(AEL_GPIO_TEST_TARGETIN);
        if (level) {
            high++;
        } else {
            low++;
        }
        if (samples > 0 && level != prev) {
            transitions++;
        }
        prev = level;
        samples++;
        esp_rom_delay_us(sample_step_us);
    }

    const int64_t actual_us = esp_timer_get_time() - start_us;
    uint32_t estimated_hz = 0;
    if (actual_us > 0 && transitions > 0) {
        estimated_hz = (uint32_t)(((uint64_t)transitions * 1000000ULL) / (2ULL * (uint64_t)actual_us));
    }

    const char *state = "unknown";
    if (transitions >= 4) {
        state = "toggle";
    } else if (high == samples && samples > 0) {
        state = "high";
    } else if (low == samples && samples > 0) {
        state = "low";
    } else {
        state = "unstable";
    }

    if (samples_out) *samples_out = samples;
    if (high_out) *high_out = high;
    if (low_out) *low_out = low;
    if (transitions_out) *transitions_out = transitions;
    if (estimated_hz_out) *estimated_hz_out = estimated_hz;
    if (state_out) *state_out = state;
}

bool gpio_loopback_test_is_supported(void)
{
    return AEL_GPIO_TEST_SUPPORTED;
}

void gpio_loopback_test_run_json(char *out_json, size_t out_json_size)
{
    if (!out_json || out_json_size == 0U) {
        return;
    }

    if (!AEL_GPIO_TEST_SUPPORTED) {
        snprintf(out_json, out_json_size,
                 "{\"test\":\"test_gpio_loopback\",\"result\":\"unsupported\",\"details\":\"board profile does not define GPIO loopback test pins\"}");
        return;
    }

    esp_err_t err = configure_test_pins();
    if (err != ESP_OK) {
        snprintf(out_json, out_json_size,
                 "{\"test\":\"test_gpio_loopback\",\"result\":\"error\",\"details\":\"pin configuration failed: %s\"}",
                 esp_err_to_name(err));
        return;
    }

    bool idle_high = wait_for_input_level(1, 10000, 50);
    bool pulse_ok = run_pulse_check(100);
    uint32_t edges_100hz = 0;
    uint32_t edges_1khz = 0;
    bool hz100_ok = run_square_wave_check(100, 10, &edges_100hz);
    bool hz1k_ok = run_square_wave_check(1000, 20, &edges_1khz);

    const char *result = (idle_high && pulse_ok && hz100_ok && hz1k_ok) ? "pass" : "fail";
    snprintf(out_json, out_json_size,
             "{\"test\":\"test_gpio_loopback\",\"result\":\"%s\",\"pins\":{\"sreset_out\":%d,\"targetin\":%d},\"checks\":{\"idle_high\":%s,\"pulse_100ms\":%s,\"freq_100hz\":%s,\"freq_1khz\":%s},\"edges\":{\"freq_100hz\":%" PRIu32 ",\"freq_1khz\":%" PRIu32 "}}",
             result,
             (int)AEL_GPIO_TEST_SRESET_OUT,
             (int)AEL_GPIO_TEST_TARGETIN,
             idle_high ? "true" : "false",
             pulse_ok ? "true" : "false",
             hz100_ok ? "true" : "false",
             hz1k_ok ? "true" : "false",
             edges_100hz,
             edges_1khz);
}

void gpio_targetin_detect_run_json(char *out_json, size_t out_json_size)
{
    if (!out_json || out_json_size == 0U) {
        return;
    }

    if (!AEL_GPIO_TEST_SUPPORTED) {
        snprintf(out_json, out_json_size,
                 "{\"test\":\"test_targetin_detect\",\"result\":\"unsupported\",\"details\":\"board profile does not define TARGETIN\"}");
        return;
    }

    esp_err_t err = configure_targetin_only();
    if (err != ESP_OK) {
        snprintf(out_json, out_json_size,
                 "{\"test\":\"test_targetin_detect\",\"result\":\"error\",\"details\":\"TARGETIN configuration failed: %s\"}",
                 esp_err_to_name(err));
        return;
    }

    uint32_t samples = 0;
    uint32_t high = 0;
    uint32_t low = 0;
    uint32_t transitions = 0;
    uint32_t estimated_hz = 0;
    const char *state = "unknown";
    sample_targetin(250, 25, &samples, &high, &low, &transitions, &estimated_hz, &state);

    const char *result = (transitions >= 4U) ? "pass" : "fail";
    snprintf(out_json, out_json_size,
             "{\"test\":\"test_targetin_detect\",\"result\":\"%s\",\"pin\":%d,\"state\":\"%s\",\"samples\":%" PRIu32 ",\"high\":%" PRIu32 ",\"low\":%" PRIu32 ",\"transitions\":%" PRIu32 ",\"estimated_hz\":%" PRIu32 "}",
             result,
             (int)AEL_GPIO_TEST_TARGETIN,
             state,
             samples,
             high,
             low,
             transitions,
             estimated_hz);
}

void gpio_uart_rxd_detect_run_json(char *out_json, size_t out_json_size)
{
    if (!out_json || out_json_size == 0U) {
        return;
    }

    if (!GPIO_IS_VALID_GPIO(AEL_GPIO_UART_RXD)) {
        snprintf(out_json, out_json_size,
                 "{\"test\":\"test_uart_rxd_detect\",\"result\":\"unsupported\",\"details\":\"board profile does not define a valid UART RX GPIO\"}");
        return;
    }

    gpio_config_t in_conf = {
        .pin_bit_mask = (1ULL << AEL_GPIO_UART_RXD),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    esp_err_t err = gpio_config(&in_conf);
    if (err != ESP_OK) {
        snprintf(out_json, out_json_size,
                 "{\"test\":\"test_uart_rxd_detect\",\"result\":\"error\",\"details\":\"UART RX GPIO configuration failed: %s\"}",
                 esp_err_to_name(err));
        return;
    }

    uint32_t samples = 0, high = 0, low = 0, transitions = 0, estimated_hz = 0;
    const char *state = "unknown";
    const int64_t start_us = esp_timer_get_time();
    const int64_t end_us = start_us + 250000LL;
    int prev = gpio_get_level(AEL_GPIO_UART_RXD);
    while (esp_timer_get_time() < end_us) {
        int level = gpio_get_level(AEL_GPIO_UART_RXD);
        if (level) high++; else low++;
        if (samples > 0 && level != prev) transitions++;
        prev = level;
        samples++;
        esp_rom_delay_us(25);
    }
    const int64_t actual_us = esp_timer_get_time() - start_us;
    if (actual_us > 0 && transitions > 0) {
        estimated_hz = (uint32_t)(((uint64_t)transitions * 1000000ULL) / (2ULL * (uint64_t)actual_us));
    }
    if (transitions >= 4U) state = "toggle";
    else if (high == samples && samples > 0) state = "high";
    else if (low == samples && samples > 0) state = "low";
    else state = "unstable";

    const char *result = (transitions >= 4U) ? "pass" : "fail";
    snprintf(out_json, out_json_size,
             "{\"test\":\"test_uart_rxd_detect\",\"result\":\"%s\",\"pin\":%d,\"state\":\"%s\",\"samples\":%" PRIu32 ",\"high\":%" PRIu32 ",\"low\":%" PRIu32 ",\"transitions\":%" PRIu32 ",\"estimated_hz\":%" PRIu32 "}",
             result,
             (int)AEL_GPIO_UART_RXD,
             state,
             samples,
             high,
             low,
             transitions,
             estimated_hz);
}
