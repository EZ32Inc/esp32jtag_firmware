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
#include <string.h>

#include "DEV_Config.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_rom_sys.h"  // For esp_rom_delay_us()

static const char *TAG_LCD = "LCD1.83";
//typedef uint16_t UWORD; // Ensure UWORD is defined

void DEV_Delay_ms(uint16_t ms) {
    if (ms < 10) {
        // For short delays (<10ms), use microsecond delay
        esp_rom_delay_us(ms * 1000);
    } else {
        // For longer delays, use FreeRTOS task delay
        vTaskDelay(pdMS_TO_TICKS(ms));
    }
}
#define LEDC_CHANNEL    LEDC_CHANNEL_0
#define LEDC_TIMER      LEDC_TIMER_0
#define LEDC_RESOLUTION LEDC_TIMER_8_BIT // 8-bit resolution (0-255)
#define LEDC_FREQUENCY  5000             // 5 kHz PWM frequency

void BK_Init(void)
{
    // Configure LEDC PWM Timer
    ledc_timer_config_t ledc_timer = {
        .speed_mode       = LEDC_LOW_SPEED_MODE,
        .timer_num        = LEDC_TIMER,
        .duty_resolution  = LEDC_RESOLUTION,
        .freq_hz          = LEDC_FREQUENCY,
        .clk_cfg          = LEDC_AUTO_CLK
    };
    ledc_timer_config(&ledc_timer);

    // Configure LEDC Channel
    ledc_channel_config_t ledc_channel = {
        .gpio_num       = DEV_BL_PIN,
        .speed_mode     = LEDC_LOW_SPEED_MODE,
        .channel        = LEDC_CHANNEL,
        .intr_type      = LEDC_INTR_DISABLE,
        .timer_sel      = LEDC_TIMER,
        .duty           = 10,//brightness,  // Set initial brightness
        .hpoint         = 0
    };
    ledc_channel_config(&ledc_channel);
}

void DEV_Set_BL(uint8_t brightness)
{
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL, brightness);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL);
}

static spi_device_handle_t spi_handle = NULL;
static spi_transaction_t spi_t;
static uint8_t spi_data[2];
void DEV_SPI_init(void)
{
    //spi init start
    esp_err_t ret;

    // SPI bus configuration
    spi_bus_config_t buscfg = {
        .mosi_io_num = PIN_LCD_MOSI,
        .miso_io_num = PIN_LCD_MISO,
        .sclk_io_num = PIN_LCD_CLK,
        .quadwp_io_num = -1,   // Not used for 4-wire SPI
        .quadhd_io_num = -1,   // Not used for 4-wire SPI
        .max_transfer_sz = 1024 // Maximum transfer size (in bytes)
    };

    // Attach the SPI bus
    ret = spi_bus_initialize(SPI_HOST_USED, &buscfg, SPI_DMA_CH_AUTO);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG_LCD, "Failed to initialize SPI bus: %s", esp_err_to_name(ret));
        return;
    }

    // Device configuration
    spi_device_interface_config_t devcfg = {
        .clock_speed_hz = 40 * 1000 * 1000, // up to 80 MHZ tested
        .mode = 0,                          // SPI mode (0, 1, 2, or 3)
        .spics_io_num = DEV_CS_PIN,         // CS pin
        .queue_size = 1,                    // Transaction queue size
    };


    // Add the SPI device
    //spi_device_handle_t spi_handle;
    ret = spi_bus_add_device(SPI_HOST_USED, &devcfg, &spi_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG_LCD, "Failed to add SPI device: %s", esp_err_to_name(ret));
        return;
    }

    memset(&spi_t, 0, sizeof(spi_t)); // Zero out the transaction
    spi_t.tx_buffer = spi_data;
    spi_t.rx_buffer = NULL;
    //spi init end
}
void DEV_SPI_WRITE(uint8_t data)
{
    // spi_transfer(data);
    spi_data[0] = data;
    spi_t.length = 8;
    spi_t.tx_buffer = spi_data;      // Data to send
    ESP_ERROR_CHECK(spi_device_transmit(spi_handle, &spi_t)); // Transmit the data
}
void DEV_SPI_WRITE_16bit(uint16_t data)
{
    // spi_transfer(data);
    spi_data[0] = data>>8;
    spi_data[1] = data;
    spi_t.tx_buffer = spi_data;      // Data to send
    spi_t.length = 16;
    ESP_ERROR_CHECK(spi_device_transmit(spi_handle, &spi_t)); // Transmit the data
}
void DEV_SPI_WRITE_DATA(uint8_t* pData, uint16_t len)
{
    spi_t.length = len*8;
    spi_t.tx_buffer = pData;
    ESP_ERROR_CHECK(spi_device_transmit(spi_handle, &spi_t)); // Transmit the data
}
uint8_t spi_transfer(uint8_t data)
{
    spi_data[0] = data;
    spi_t.tx_buffer = spi_data;      // Data to send
    spi_t.length = 8;
    ESP_ERROR_CHECK(spi_device_transmit(spi_handle, &spi_t)); // Transmit the data
    return 0; //data; // Return received data (optional)
}

void GPIO_Init()
{
    // Configure LED pin as output
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << DEV_RST_PIN),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    gpio_config(&io_conf);

    // Configure Button pin as input with pull-up
    io_conf.pin_bit_mask = (1ULL << DEV_DC_PIN);
    gpio_config(&io_conf);

    BK_Init();

    // Call this instead of analogWrite()
    //DEV_Set_BL(30); //23.436us/200us, 11.7%
    DEV_Set_BL(40); //31.246us/200us, 15.62%
                    //DEV_Set_BL(60); //46.872us/200us, 23%
                    //DEV_Set_BL(120);//93.748us/200us, 46.87% 
}
void Config_Init()
{
    GPIO_Init();
    DEV_SPI_init();
}

