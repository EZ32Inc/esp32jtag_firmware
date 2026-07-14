/*****************************************************************************
* | File        :   GUI_Paint.h
* | Author      :   Waveshare team
* | Function    : Achieve drawing: draw points, lines, boxes, circles and
*                   their size, solid dotted line, solid rectangle hollow
*                   rectangle, solid circle hollow circle.
* | Info        :
*   Achieve display characters: Display a single character, string, number
*   Achieve time display: adaptive size display time minutes and seconds
*----------------
* | This version:   V1.0
* | Date        :   2018-11-15
* | Info        :    
*
* Permission is hereby granted, free of charge, to any person obtaining a copy
* of this software and associated documnetation files (the "Software"), to deal
* in the Software without restriction, including without limitation the rights
* to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
* copies of the Software, and to permit persons to  whom the Software is
* furished to do so, subject to the following conditions:
*
* The above copyright notice and this permission notice shall be included in
* all copies or substantial portions of the Software.
*
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
* IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
* FITNESS OR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
* AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
* LIABILITY WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
* OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
* THE SOFTWARE.
*
******************************************************************************/
#pragma once

#include "DEV_Config.h"
#include "LCD_Driver.h"
#include "fonts.h"
#include "Debug.h"
#include <pgmspace.h>

#define X_START_INFO 20
#define X_MARGIN 10
#define X_OFFSET X_START_INFO + X_MARGIN
#define Y_START_INFO 154 //10+24*6

#define CURR_FONT Font16

#define PAINT_CHAR_WIDTH (12)

void print_info(const char * pString);

#define SWAP16BIT(C) (((C >>8)&0xff) | ((C<<8) &0xff00))

/**
 * Image attributes
**/
typedef struct {
    UBYTE *Image;
    UWORD Width;
    UWORD Height;
    UWORD WidthMemory;
    UWORD HeightMemory;
    UWORD Color;
    UWORD Rotate;
    UWORD Mirror;
    UWORD WidthByte;
    UWORD HeightByte;
} PAINT;
extern volatile PAINT Paint;

/**
 * Display rotate
**/
#define ROTATE_0            0
#define ROTATE_90           90
#define ROTATE_180          180
#define ROTATE_270          270

/**
 * Display Flip
**/
typedef enum {
    MIRROR_NONE  = 0x00,
    MIRROR_HORIZONTAL = 0x01,
    MIRROR_VERTICAL = 0x02,
    MIRROR_ORIGIN = 0x03,
} MIRROR_IMAGE;
#define MIRROR_IMAGE_DFT MIRROR_NONE

/**
 * image color
**/

#define WHITE         SWAP16BIT(0xFFFF)
#define BLACK         SWAP16BIT(0x0000)    
#define BLUE          SWAP16BIT(0x001F)  
#define BRED          SWAP16BIT(0XF81F)
#define GRED          SWAP16BIT(0XFFE0)
#define GBLUE         SWAP16BIT(0X07FF)
#define RED           SWAP16BIT(0xF800)
#define MAGENTA       SWAP16BIT(0xF81F)
#define GREEN         SWAP16BIT(0x07E0)
#define CYAN          SWAP16BIT(0x7FFF)
#define YELLOW        SWAP16BIT(0xFFE0)
#define BROWN         SWAP16BIT(0XBC40) 
#define BRRED         SWAP16BIT(0XFC07) 
#define GRAY          SWAP16BIT(0X8430) 
#define DARKBLUE      SWAP16BIT(0X01CF)  
#define LIGHTBLUE     SWAP16BIT(0X7D7C)   
#define GRAYBLUE      SWAP16BIT(0X5458) 
#define LIGHTGREEN    SWAP16BIT(0X841F) 
#define LGRAY         SWAP16BIT(0XC618) 
#define LGRAYBLUE     SWAP16BIT(0XA651)
#define LBBLUE        SWAP16BIT(0X2B12) 


#define IMAGE_BACKGROUND    WHITE
#define FONT_FOREGROUND     BLACK
#define FONT_BACKGROUND     WHITE

/**
 * The size of the point
**/
typedef enum {
    DOT_PIXEL_1X1  = 1,   // 1 x 1
    DOT_PIXEL_2X2  ,    // 2 X 2
    DOT_PIXEL_3X3  ,    // 3 X 3
    DOT_PIXEL_4X4  ,    // 4 X 4
    DOT_PIXEL_5X5  ,    // 5 X 5
    DOT_PIXEL_6X6  ,    // 6 X 6
    DOT_PIXEL_7X7  ,    // 7 X 7
    DOT_PIXEL_8X8  ,    // 8 X 8
} DOT_PIXEL;
#define DOT_PIXEL_DFT  DOT_PIXEL_1X1  //Default dot pilex

/**
 * Point size fill style
**/
typedef enum {
    DOT_FILL_AROUND  = 1,   // dot pixel 1 x 1
    DOT_FILL_RIGHTUP  ,     // dot pixel 2 X 2
} DOT_STYLE;
#define DOT_STYLE_DFT  DOT_FILL_AROUND  //Default dot pilex

/**
 * Line style, solid or dashed
**/
typedef enum {
    LINE_STYLE_SOLID = 0,
    LINE_STYLE_DOTTED,
} LINE_STYLE;

/**
 * Whether the graphic is filled
**/
typedef enum {
    DRAW_FILL_EMPTY = 0,
    DRAW_FILL_FULL,
} DRAW_FILL;

/**
 * Custom structure of a time attribute
**/
typedef struct {
    UWORD Year;  //0000
    UBYTE  Month; //1 - 12
    UBYTE  Day;   //1 - 30
    UBYTE  Hour;  //0 - 23
    UBYTE  Min;   //0 - 59
    UBYTE  Sec;   //0 - 59
} PAINT_TIME;
extern PAINT_TIME sPaint_time;

//init and Clear
void Paint_NewImage(UWORD Width, UWORD Height, UWORD Rotate, UWORD Color);
void Paint_SetRotate(UWORD Rotate);
void Paint_SetMirroring(UBYTE mirror);
void Paint_SetPixel(UWORD Xpoint, UWORD Ypoint, UWORD Color);

void Paint_Clear(UWORD Color);
void Paint_ClearWindows(UWORD Xstart, UWORD Ystart, UWORD Xend, UWORD Yend, UWORD Color);

//Drawing
void Paint_DrawPoint(UWORD Xpoint, UWORD Ypoint, UWORD Color, DOT_PIXEL Dot_Pixel, DOT_STYLE Dot_FillWay);
void Paint_DrawLine(UWORD Xstart, UWORD Ystart, UWORD Xend, UWORD Yend, UWORD Color, DOT_PIXEL Line_width, LINE_STYLE Line_Style);
void Paint_DrawRectangle(UWORD Xstart, UWORD Ystart, UWORD Xend, UWORD Yend, UWORD Color, DOT_PIXEL Line_width, DRAW_FILL Filled );
void Paint_DrawCircle(UWORD X_Center, UWORD Y_Center, UWORD Radius, UWORD Color, DOT_PIXEL Line_width, DRAW_FILL Draw_Fill );

//Display string
void Paint_DrawChar(UWORD Xstart, UWORD Ystart, const char Acsii_Char, sFONT* Font, UWORD Color_Background, UWORD Color_Foreground);
void Paint_DrawString_EN(UWORD Xstart, UWORD Ystart, const char * pString, sFONT* Font, UWORD Color_Background, UWORD Color_Foreground);
void Paint_DrawString_CN(UWORD Xstart, UWORD Ystart, const char * pString, cFONT* font, UWORD Color_Background, UWORD Color_Foreground);
void Paint_DrawNum(UWORD Xpoint, UWORD Ypoint, int32_t Nummber, sFONT* Font, UWORD Color_Background, UWORD Color_Foreground);
void Paint_DrawFloatNum(UWORD Xpoint, UWORD Ypoint, double Nummber,  UBYTE Decimal_Point, sFONT* Font, UWORD Color_Background, UWORD Color_Foreground);
void Paint_DrawTime(UWORD Xstart, UWORD Ystart, PAINT_TIME *pTime, sFONT* Font, UWORD Color_Background, UWORD Color_Foreground);

//pic
void Paint_DrawImage(const unsigned char *image, UWORD Startx, UWORD Starty, UWORD Endx, UWORD Endy);

void lcd_clear(void);
