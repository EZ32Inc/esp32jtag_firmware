//Ref: https://blog.csdn.net/lmlwz123/article/details/142408129
//
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h> // For memset

#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_commands.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_err.h"
#include "esp_log.h"
#include "lcd_library.h"
//#include "lcd.h"
//#include "GUI_Paint.h"

#define LCD_SPI_HOST    SPI3_HOST
#define USING_PWM_BKL 1
static const char* TAG = "NV3030B";

//lcd操作句柄
static esp_lcd_panel_io_handle_t lcd_io_handle = NULL;

//刷新完成回调函数
static lcd_flush_done_cb    s_flush_done_cb = NULL;

static bool notify_flush_ready(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_io_event_data_t *edata, void *user_ctx)
{
    if(s_flush_done_cb)
        s_flush_done_cb(user_ctx);
    return false;
}


extern void DEV_Set_BL(uint8_t brightness);
extern void BK_Init(void);

esp_err_t driver_hw_init(cfg_t* cfg)
{
    //初始化SPI
    spi_bus_config_t buscfg = {
        .sclk_io_num = cfg->clk,
        .mosi_io_num = cfg->mosi,
        .miso_io_num = cfg->miso,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .flags = SPICOMMON_BUSFLAG_MASTER,
        .max_transfer_sz = cfg->width * 40 * sizeof(uint16_t),  //nmb of bytes for a DMA single tx/rx, max 32768
    };
    ESP_ERROR_CHECK(spi_bus_initialize(LCD_SPI_HOST, &buscfg, SPI_DMA_CH_AUTO));

    s_flush_done_cb = cfg->done_cb; //设置刷新完成回调函数
#ifndef USING_PWM_BKL  
    gpio_config_t bl_gpio_cfg = 
    {
        .pull_up_en = GPIO_PULLUP_DISABLE,          //禁止上拉
        .pull_down_en = GPIO_PULLDOWN_DISABLE,      //禁止下拉
        .mode = GPIO_MODE_OUTPUT,                   //输出模式
        .intr_type = GPIO_INTR_DISABLE,             //禁止中断
        .pin_bit_mask = (1ULL<<cfg->bl)                //GPIO脚
    };
    gpio_config(&bl_gpio_cfg);
#else
    BK_Init();
#endif

    //初始化复位脚
    if(cfg->rst > 0)
    {
        gpio_config_t rst_gpio_cfg = 
        {
            .pull_up_en = GPIO_PULLUP_DISABLE,          //禁止上拉
            .pull_down_en = GPIO_PULLDOWN_DISABLE,      //禁止下拉
            .mode = GPIO_MODE_OUTPUT,                   //输出模式
            .intr_type = GPIO_INTR_DISABLE,             //禁止中断
            .pin_bit_mask = (1ULL<<cfg->rst)                //GPIO脚
        };
        gpio_config(&rst_gpio_cfg);
    }

    //创建基于spi的lcd操作句柄
    esp_lcd_panel_io_spi_config_t io_config = {
        .dc_gpio_num = cfg->dc,         //DC引脚
        .cs_gpio_num = cfg->cs,         //CS引脚
        .pclk_hz = cfg->spi_fre,        //SPI时钟频率
        .lcd_cmd_bits = 8,              //命令长度
        .lcd_param_bits = 8,            //参数长度
        .spi_mode = 0,                  //使用SPI0模式
        .trans_queue_depth = 10,        //表示可以缓存的spi传输事务队列深度
        .on_color_trans_done = notify_flush_ready,   //刷新完成回调函数
        .user_ctx = cfg->cb_param,                                    //回调函数参数
        .flags = {    // 以下为 SPI 时序的相关参数，需根据 LCD 驱动 IC 的数据手册以及硬件的配置确定
            .sio_mode = 0,    // 通过一根数据线（MOSI）读写数据，0: Interface I 型，1: Interface II 型
        },
    };
    // Attach the LCD to the SPI bus
    ESP_LOGI(TAG,"create esp_lcd_new_panel");
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)LCD_SPI_HOST, &io_config, &lcd_io_handle));

    //硬件复位
    if(cfg->rst > 0)
    {
        ESP_LOGI(TAG,"toggling cfg->rst 0 then to 1");
        gpio_set_level(cfg->rst,0);
        vTaskDelay(pdMS_TO_TICKS(100));
        gpio_set_level(cfg->rst,1);
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    esp_lcd_panel_io_tx_param(lcd_io_handle,0xfd,(uint8_t[]){0x06,0x08},2); 
    esp_lcd_panel_io_tx_param(lcd_io_handle,0x61,(uint8_t[]){0x07,0x04},2); 
    esp_lcd_panel_io_tx_param(lcd_io_handle,0x62,(uint8_t[]){0x00,0x44,0x45},3); 
    esp_lcd_panel_io_tx_param(lcd_io_handle,0x63,(uint8_t[]){0x41,0x07,0x12,0x12},4); 
    esp_lcd_panel_io_tx_param(lcd_io_handle,0x64,(uint8_t[]){0x37},1); 
    //VSP
    esp_lcd_panel_io_tx_param(lcd_io_handle,0x65,(uint8_t[]){0x09,0x10,0x21},3); 
    esp_lcd_panel_io_tx_param(lcd_io_handle,0x66,(uint8_t[]){0x09,0x10,0x21},3);    
    esp_lcd_panel_io_tx_param(lcd_io_handle,0x67,(uint8_t[]){0x20,0x40},2);  
    esp_lcd_panel_io_tx_param(lcd_io_handle,0x68,(uint8_t[]){0x90,0x4c,0x7C,0x66},4);  
    esp_lcd_panel_io_tx_param(lcd_io_handle,0xb1,(uint8_t[]){0x0F,0x08,0x01},3);  
    esp_lcd_panel_io_tx_param(lcd_io_handle,0xB4,(uint8_t[]){0x01},1); 
    esp_lcd_panel_io_tx_param(lcd_io_handle,0xB5,(uint8_t[]){0x02,0x02,0x0a,0x14},4); 
    esp_lcd_panel_io_tx_param(lcd_io_handle,0xB6,(uint8_t[]){0x04,0x01,0x9f,0x00,0x02},5);
    esp_lcd_panel_io_tx_param(lcd_io_handle,0xdf,(uint8_t[]){0x11},1);
    esp_lcd_panel_io_tx_param(lcd_io_handle,0xE2,(uint8_t[]){0x13,0x00,0x00,0x30,0x33,0x3f},6);
    esp_lcd_panel_io_tx_param(lcd_io_handle,0xE5,(uint8_t[]){0x3f,0x33,0x30,0x00,0x00,0x13},6);
    esp_lcd_panel_io_tx_param(lcd_io_handle,0xE1,(uint8_t[]){0x00,0x57},2);
    esp_lcd_panel_io_tx_param(lcd_io_handle,0xE4,(uint8_t[]){0x58,0x00},2);
    esp_lcd_panel_io_tx_param(lcd_io_handle,0xE0,(uint8_t[]){0x01,0x03,0x0d,0x0e,0x0e,0x0c,0x15,0x19},8);
    esp_lcd_panel_io_tx_param(lcd_io_handle,0xE3,(uint8_t[]){0x1a,0x16,0x0C,0x0f,0x0e,0x0d,0x02,0x01},8);
    esp_lcd_panel_io_tx_param(lcd_io_handle,0xE6,(uint8_t[]){0x00,0xff},2);
    esp_lcd_panel_io_tx_param(lcd_io_handle,0xE7,(uint8_t[]){0x01,0x04,0x03,0x03,0x00,0x12},6);
    esp_lcd_panel_io_tx_param(lcd_io_handle,0xE8,(uint8_t[]){0x00,0x70,0x00},3);
    esp_lcd_panel_io_tx_param(lcd_io_handle,0xEC,(uint8_t[]){0x52},1);
    esp_lcd_panel_io_tx_param(lcd_io_handle,0xF1,(uint8_t[]){0x01,0x01,0x02},3);

    esp_lcd_panel_io_tx_param(lcd_io_handle,0xF6,(uint8_t[]){0x09,0x10,0x00,0x00},4);
    esp_lcd_panel_io_tx_param(lcd_io_handle,0xfd,(uint8_t[]){0xfa,0xfc},2);
    esp_lcd_panel_io_tx_param(lcd_io_handle,0x3a,(uint8_t[]){0x05},1);
    esp_lcd_panel_io_tx_param(lcd_io_handle,0x35,(uint8_t[]){0x00},1);


    if(USE_HORIZONTAL==0) esp_lcd_panel_io_tx_param(lcd_io_handle,0x36,(uint8_t[]){0x08},1);
    else if(USE_HORIZONTAL==1)esp_lcd_panel_io_tx_param(lcd_io_handle,0x36,(uint8_t[]){0xC8},1);
    else if(USE_HORIZONTAL==2)esp_lcd_panel_io_tx_param(lcd_io_handle,0x36,(uint8_t[]){0x78},1);
    else esp_lcd_panel_io_tx_param(lcd_io_handle,0x36,(uint8_t[]){0xA8},1);


    esp_lcd_panel_io_tx_param(lcd_io_handle,0x21,NULL,0);
    //LCD_WR_REG(0x21); 
    esp_lcd_panel_io_tx_param(lcd_io_handle,0x11,NULL,0); 
    vTaskDelay(pdMS_TO_TICKS(200));
    esp_lcd_panel_io_tx_param(lcd_io_handle,0x29,NULL,0); 
    vTaskDelay(pdMS_TO_TICKS(10));       

    ESP_LOGI(TAG,"To set lcd_backlight(1)");
#ifndef USING_PWM_BKL  
    lcd_backlight(cfg->bl, true);//打开背光
#else
    DEV_Set_BL(60); //60: 46.872us/200us, 23% ;
#endif

    return ESP_OK;
}

/** lcd写入显示数据
 * @param x1,x2,y1,y2:显示区域
 * @return 无
 */
void paint_rect(int x1,int x2,int y1,int y2,void *data)
{
    //ESP_LOGI(TAG, "to do paint_rect(). len = %d", (x2 - x1) * (y2 - y1) * 2);
    // define an area of frame memory where MCU can access
    if(x2 <= x1 || y2 <= y1)
    {
        if(s_flush_done_cb)
            s_flush_done_cb(NULL);
        return;
    }
    esp_lcd_panel_io_tx_param(lcd_io_handle, LCD_CMD_CASET, (uint8_t[]) {
            (x1 >> 8) & 0xFF,
            x1 & 0xFF,
            ((x2 - 1) >> 8) & 0xFF,
            (x2 - 1) & 0xFF,
            }, 4);
    esp_lcd_panel_io_tx_param(lcd_io_handle, LCD_CMD_RASET, (uint8_t[]) {
            (y1 >> 8) & 0xFF,
            y1 & 0xFF,
            ((y2 - 1) >> 8) & 0xFF,
            (y2 - 1) & 0xFF,
            }, 4);

    size_t len = (x2 - x1) * (y2 - y1) * 2;
    //ESP_LOGI(TAG, " transfer frame buffer. len = %d", len);
    esp_lcd_panel_io_tx_color(lcd_io_handle, LCD_CMD_RAMWR, data, len);
    return ;
}

/** 控制背光
 * @param enable 是否使能背光
 * @return 无
 */
void lcd_backlight(gpio_num_t bl_pin, bool enable)
{
    gpio_set_level(bl_pin, enable? 1:0);
}

void lcd_init(void)
{
    cfg_t config;
    config.miso = GPIO_NUM_6;
    config.mosi = GPIO_NUM_10;
    config.clk = GPIO_NUM_8;
    config.cs = GPIO_NUM_9;
    config.dc = GPIO_NUM_18;
    config.rst = GPIO_NUM_7;
    config.bl = GPIO_NUM_17;
#if USING_ARTIX7_BOARD
    config.spi_fre = 26*1000*1000; //SPI frequency, could be up to 40MHz
#else
    config.spi_fre = 40*1000*1000; //SPI frequency, could be up to 40MHz
#endif
    //config.spi_fre = 80*1000*1000; //SPI frequency, could be up to 80MHz
    config.width = LCD_W;            //屏宽
    config.height = LCD_H;          //屏高
    config.spin = 1;                     //顺时针旋转90度
    config.done_cb = NULL;//lv_port_flush_ready;    //数据写入完成回调函数
    config.cb_param = NULL;//&disp_drv;         //回调函数参数

    driver_hw_init(&config);
}

#if 0
extern void lcd_clear(void);
void app_main(void)
{
    lcd_init();
    lcd_clear();
    while(1)
    {
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
#endif
