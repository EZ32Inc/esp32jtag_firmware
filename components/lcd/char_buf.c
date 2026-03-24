#include "LCD_Driver.h"
#include <stdlib.h>  // For malloc
#include <stdint.h>  // For uint8_t
#include <stdbool.h> // For bool

//Global
uint8_t *pChar_buf = NULL;
size_t ch_buffer_size = 0;

bool init_char_buf(void)
{
    if (pChar_buf == NULL) {
        // Allocate buffer based on maximum of LCD height or width
        //2 bytes per pixel
        ch_buffer_size = FONT_HEIGHT * ((LCD_HEIGHT > LCD_WIDTH) ? LCD_HEIGHT : LCD_WIDTH)*2; 
        pChar_buf = (uint8_t *)malloc(ch_buffer_size);
        if (pChar_buf == NULL) {
            // Allocation failed
            return false;
        }
    }
    return true;
}
