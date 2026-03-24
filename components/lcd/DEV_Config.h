/*****************************************************************************
* | File        :   DEV_Config.c
* | Author      :   Waveshare team
* | Function    :   Hardware underlying interface
* | Info        :
*                Used to shield the underlying layers of each master 
*                and enhance portability
*----------------
* | This version:   V1.0
* | Date        :   2018-11-22
* | Info        :

# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documnetation files (the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to  whom the Software is
# furished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in
# all copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS OR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
# THE SOFTWARE.
#
******************************************************************************/
#pragma once

#include <stdint.h>
#include <stdio.h>
#include "driver/spi_master.h"
#include "Debug.h"
#include <pgmspace.h>
#include "driver/gpio.h"
#include "esp_log.h"

/* Legacy type aliases — use stdint.h types in new code */
#define UBYTE   uint8_t
#define UWORD   uint16_t
#define UDOUBLE uint32_t

/**
 * GPIO config
**/
#define PIN_LCD_MOSI (10)
#define PIN_LCD_MISO (6)
#define PIN_LCD_CLK  (8)
#define DEV_RST_PIN  (7)
#define DEV_DC_PIN   (18)
#define DEV_CS_PIN   (9)
#define DEV_BL_PIN   (17)

#define SPI_HOST_USED SPI3_HOST  // SPI3_HOST for the other SPI controller

void DEV_SPI_init(void);

#define DEV_Digital_Write(_pin, _value) gpio_set_level(_pin, _value)
#define DEV_Digital_Read(_pin, _value)  gpio_get_level(_pin)

uint8_t spi_transfer(uint8_t data);
void DEV_SPI_WRITE(uint8_t data);
void DEV_SPI_WRITE_16bit(uint16_t data);
void DEV_SPI_WRITE_DATA(uint8_t *pData, uint16_t len);

void DEV_Delay_ms(uint16_t ms);

void BK_Init(void);
void DEV_Set_BL(uint8_t brightness);
void GPIO_Init(void);
void Config_Init(void);
