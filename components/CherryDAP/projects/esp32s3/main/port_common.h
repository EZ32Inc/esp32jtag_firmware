#pragma once
#include <stdint.h>
#include "gpio.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "driver/gpio.h"
#include "driver/uart.h"
#include "driver/uart_select.h"
#include "esp_log.h"
//#include "../../../../../main/network/uart_websocket.h"
#include "../../../../../main/esp32jtag_common.h"

#ifndef __STATIC_INLINE
#define __STATIC_INLINE static inline
#endif

#ifndef __WEAK
#define __WEAK __attribute__((weak))
#endif

#ifndef SWDIO_RDnWR_PIN
#define SWDIO_RDnWR_PIN (45)
#endif

#define PIN_SWCLK_TCK               GPIO_NUM_47
#define PIN_SWDIO_TMS               GPIO_NUM_41
#define PIN_TDI                     GPIO_NUM_40
#define PIN_TDO                     GPIO_NUM_15
//#define PIN_nRESET                  GPIO_NUM_6
// #define LED_CONNECTED               GPIO_NUM_41
// #define LED_RUNNING                 GPIO_NUM_42

#define DAP_UART_TX   GPIO_UART_TXD
#define DAP_UART_RX   GPIO_UART_RXD

//#define USART_UX                    UART_NUM_0
//#define DEV_UART0_TX                GPIO_NUM_16
//#define DEV_UART0_RX                GPIO_NUM_3
//#define RX_BUF_SIZE                 1024

#define DAP_CPU_CLOCK 0U

void dap_platform_init(void);
void dap_gpio_init(void);

void set_led_connect(uint32_t bit);
void set_led_running(uint32_t bit);
uint32_t get_led_connect(void);
uint32_t get_led_running(void);


static inline uint32_t dap_get_time_stamp(void)
{
    return (uint32_t)xTaskGetTickCount();
}
#define CHERRY_GPIO_OUTPUT_PIN_SEL  (((AEL_BMP_HAS_SWDIO_RDNWR ? (1ULL << SWDIO_RDnWR_PIN) : 0ULL)) | (1ULL<< PIN_SWCLK_TCK) | (1ULL<<PIN_SWDIO_TMS) | \
        (1ULL<<PIN_TDI) | (1ULL<<PIN_TDI) | (1ULL<<PIN_TDO) )
//#define GPIO_OUTPUT_PIN_SEL  (BIT(SWDIO_RDnWR_PIN) | BIT(SWCLK_PIN) | BIT(SWDIO_PIN) | BIT(TMS_PIN) | BIT(TDI_PIN) | BIT(TDO_PIN) | BIT(TCK_PIN))

static inline void pins_init_cherry() {

    ESP_LOGI("pins_init_cherry", "doing pins_init");

    gpio_config_t io_conf;
    //disable interrupt
    io_conf.intr_type = GPIO_INTR_DISABLE;
    //set as output mode
    io_conf.mode = GPIO_MODE_OUTPUT;
    //bit mask of the pins that you want to set
    io_conf.pin_bit_mask = CHERRY_GPIO_OUTPUT_PIN_SEL;
    //disable pull-down mode
    io_conf.pull_down_en = 0;
    //disable pull-up mode
    io_conf.pull_up_en = 0;
    //configure GPIO with the given settings
    gpio_config(&io_conf);

    //set SWDIO_RDnWR_PIN when present
    if (AEL_BMP_HAS_SWDIO_RDNWR) {
        gpio_set_direction(SWDIO_RDnWR_PIN, GPIO_MODE_OUTPUT);
    }

    //SWDIO_MODE_DRIVE();

    //set TDO:
    gpio_set_direction(PIN_TDO, GPIO_MODE_INPUT);
    //gpio_set_pull_mode(TDO_PIN, GPIO_FLOATING);

}

