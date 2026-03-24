#include "driver/spi_master.h"

#include "LCD_Driver.h"
#include "GUI_Paint.h"
#include "image.h"

static const char *TAG = "LCD_setup";
static void LCD_setup(void)
{
    ESP_LOGI(TAG, "Config_Init");
    Config_Init();
    ESP_LOGI(TAG, "LCD_Init");
    LCD_Init();
    ESP_LOGI(TAG, "LCD_Clear");
    LCD_Clear(WHITE);
    ESP_LOGI(TAG, "DEV_Set_BL");
    //LCD_SetBacklight(100); 
    DEV_Set_BL(60); //60: 46.872us/200us, 23% ;
    ESP_LOGI(TAG, "Paint_NewImage");
    Paint_NewImage(LCD_WIDTH, LCD_HEIGHT, 0, WHITE);
    ESP_LOGI(TAG, "Paint_Clear");
    Paint_Clear(WHITE);
    ESP_LOGI(TAG, "Paint_SetRotate");
    Paint_SetRotate(ROTATE_0);
    ESP_LOGI(TAG, "Paint_DrawString_EN 123");

    //Paint_DrawString_EN(30, 10, "1234567890",        &Font24,  YELLOW, RED);  
    //Paint_DrawString_EN(0, 10, "ABCDEFGH.IJKLMNOPQRSTUVWXyzabCD",        &Font24,  YELLOW, RED);  
    Paint_DrawString_EN(0, 10, "IPADDRESS",        &Font24,  YELLOW, RED);
    //LCD_ClearWindow(10, 150, 240, 280,RED);
    LCD_ClearWindow(10, 100, 200, 140,GREEN);
    LCD_ClearWindow(10, 150, 220, 240, RED);
    ESP_LOGI(TAG, "LCD_setup() done!");
}
/*********************************************************************************************************
  END FILE
 *********************************************************************************************************/
