/*****************************************************************************
  | File        :   GUI_Paint.c
  | Author      :   Waveshare team
  | Function    : Achieve drawing: draw points, lines, boxes, circles and
                    their size, solid dotted line, solid rectangle hollow
                    rectangle, solid circle hollow circle.
  | Info        :
    Achieve display characters: Display a single character, string, number
    Achieve time display: adaptive size display time minutes and seconds
  ----------------
  | This version:   V1.0
  | Date        :   2018-11-15
  | Info        :

  Permission is hereby granted, free of charge, to any person obtaining a copy
  of this software and associated documnetation files (the "Software"), to deal
  in the Software without restriction, including without limitation the rights
  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
  copies of the Software, and to permit persons to  whom the Software is
  furished to do so, subject to the following conditions:

  The above copyright notice and this permission notice shall be included in
  all copies or substantial portions of the Software.

  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
  FITNESS OR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
  LIABILITY WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
  THE SOFTWARE.

******************************************************************************/
#include <stdint.h>
#include <stdlib.h>
#include <string.h> //memset()
#include <math.h>
#include "GUI_Paint.h"
#include "DEV_Config.h"
#include "lcd_library.h"

volatile PAINT Paint;
//static uint16_t pBuf[300];
static const char *TAG = "PAINT";

void print_info(const char * pString)
{
    Paint_DrawString_EN(X_OFFSET, Y_START_INFO, "                ", &CURR_FONT, BLACK, GREEN);//clear
    Paint_DrawString_EN(X_OFFSET, Y_START_INFO, pString, &CURR_FONT, BLACK, GREEN);
}


void lcd_clear(void)
{
    ESP_LOGI("PSRAM", "Free PSRAM: %d bytes", heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
    vTaskDelay(100 / portTICK_PERIOD_MS);

    uint8_t *data = heap_caps_malloc(100 * 100 * 2, MALLOC_CAP_INTERNAL);

    if(!data){
        ESP_LOGE(TAG, "Error: malloc() failed\n");
        return;
    }
    //ESP_LOGI("PSRAM", "PSRAM buffer allocated at %p", data);

    //Clear screen
    //memset(data, 0, 100*100*2);
    uint16_t *ptr = (uint16_t *) data;
    for(int i=0;i<100*100;++i){
        ptr[i]= BLACK;
    }
    //paint_rect(0,100,0,100, data);
    //        paint_rect(100,200,100,200, data);
    paint_rect(220,320,0,100, data);
    paint_rect(220,320,100,200, data);
    paint_rect(220,320,200,240, data);
    paint_rect(120,220,0,100, data);
    paint_rect(120,220,100,200, data);
    paint_rect(120,220,200,240, data);
    paint_rect(20,120,0,100, data);
    paint_rect(20,120,100,200, data);
    paint_rect(20,120,200,240, data);
    paint_rect(0,20,0,100, data);
    paint_rect(0,20,100,200, data);
    paint_rect(0,20,200,240, data);
    free(data);
}
/******************************************************************************
function: Create Image
parameter:
image   :   Pointer to the image cache
width   :   The width of the picture
Height  :   The height of the picture
Color   :   Whether the picture is inverted
 ******************************************************************************/
void Paint_NewImage(UWORD Width, UWORD Height, UWORD Rotate, UWORD Color)
{
    Paint.WidthMemory = Width;
    Paint.HeightMemory = Height;
    Paint.Color = Color;
    Paint.WidthByte = Width;
    Paint.HeightByte = Height;

    Paint.Rotate = Rotate;
    Paint.Mirror = MIRROR_NONE;

    if (Rotate == ROTATE_0 || Rotate == ROTATE_180) {
        Paint.Width = Width;
        Paint.Height = Height;
    } else {
        Paint.Width = Height;
        Paint.Height = Width;
    }
}

/******************************************************************************
function: Select Image Rotate
parameter:
Rotate   :   0,90,180,270
 ******************************************************************************/
void Paint_SetRotate(UWORD Rotate)
{
    if (Rotate == ROTATE_0 || Rotate == ROTATE_90 || Rotate == ROTATE_180 || Rotate == ROTATE_270) {
        //Debug("Set image Rotate %d\r\n", Rotate);
        Paint.Rotate = Rotate;
    } else {
        //Debug("rotate = 0, 90, 180, 270\r\n");
        //  exit(0);
    }
}

/******************************************************************************
function: Select Image mirror
parameter:
mirror   :       Not mirror,Horizontal mirror,Vertical mirror,Origin mirror
 ******************************************************************************/
void Paint_SetMirroring(UBYTE mirror)
{
    if (mirror == MIRROR_NONE || mirror == MIRROR_HORIZONTAL ||
            mirror == MIRROR_VERTICAL || mirror == MIRROR_ORIGIN) {
        //Debug("mirror image x:%s, y:%s\r\n", (mirror & 0x01) ? "mirror" : "none", ((mirror >> 1) & 0x01) ? "mirror" : "none");
        Paint.Mirror = mirror;
    } else {
        /*
           Debug("mirror should be MIRROR_NONE, MIRROR_HORIZONTAL, \
           MIRROR_VERTICAL or MIRROR_ORIGIN\r\n");
           exit(0);
           */
    }
}

/******************************************************************************
  function: Draw Pixels
  parameter:
    Xpoint  :   At point X
    Ypoint  :   At point Y
    Color   :   Painted colors
******************************************************************************/
void Paint_SetPixel(UWORD Xpoint, UWORD Ypoint, UWORD Color)
{
  if (Xpoint > Paint.Width || Ypoint > Paint.Height) {
    //Debug("Exceeding display boundaries\r\n");
    return;
  }
  UWORD X, Y;

  switch (Paint.Rotate) {
    case 0:
      X = Xpoint;
      Y = Ypoint;
      break;
    case 90:
      X = Paint.WidthMemory - Ypoint - 1;
      Y = Xpoint;
      break;
    case 180:
      X = Paint.WidthMemory - Xpoint - 1;
      Y = Paint.HeightMemory - Ypoint - 1;
      break;
    case 270:
      X = Ypoint;
      Y = Paint.HeightMemory - Xpoint - 1;
      break;

    default:
      return;
  }

  switch (Paint.Mirror) {
    case MIRROR_NONE:
      break;
    case MIRROR_HORIZONTAL:
      X = Paint.WidthMemory - X - 1;
      break;
    case MIRROR_VERTICAL:
      Y = Paint.HeightMemory - Y - 1;
      break;
    case MIRROR_ORIGIN:
      X = Paint.WidthMemory - X - 1;
      Y = Paint.HeightMemory - Y - 1;
      break;
    default:
      return;
  }

  // printf("x = %d, y = %d\r\n", X, Y);
  if (X > Paint.WidthMemory || Y > Paint.HeightMemory) {
    //Debug("Exceeding display boundaries\r\n");
    return;
  }

  // UDOUBLE Addr = X / 8 + Y * Paint.WidthByte;
  LCD_SetUWORD(X, Y, Color);
}

/******************************************************************************
  function: Clear the color of the picture
  parameter:
    Color   :   Painted colors
******************************************************************************/
void Paint_Clear(UWORD Color)
{
    if(pChar_buf==NULL)
    {
        bool ret=init_char_buf();
        if(!ret){
            ESP_LOGE("MEMALLOC", "Falied!");
            return;
        }
        ESP_LOGI("MEMALLOC", "Sucessfull!");
    }
    for (UWORD X = 0; X < Paint.WidthByte; X++)
        pChar_buf[X] = Color;
    LCD_SetCursor(0, 0, Paint.WidthByte-1 , Paint.HeightByte-1);
    for (UWORD Y = 0; Y < Paint.HeightByte; Y++) {
        DEV_SPI_WRITE_DATA((uint8_t *)pChar_buf, Paint.WidthByte * 2);
    }
}

/******************************************************************************
function: Clear the color of a window
parameter:
Xstart :   x starting point
Ystart :   Y starting point
Xend   :   x end point
Yend   :   y end point
 ******************************************************************************/
void Paint_ClearWindows(UWORD Xstart, UWORD Ystart, UWORD Xend, UWORD Yend, UWORD Color)
{
    UWORD X, Y;
    for (Y = Ystart; Y < Yend; Y++) {
        for (X = Xstart; X < Xend; X++) {//8 pixel =  1 byte
            Paint_SetPixel(X, Y, Color);
        }
    }
}

/******************************************************************************
function:	Draw Point(Xpoint, Ypoint) Fill the color
parameter:
Xpoint		:   The Xpoint coordinate of the point
Ypoint		:   The Ypoint coordinate of the point
Color		:   Set color
Dot_Pixel	:	point size
 ******************************************************************************/
void Paint_DrawPoint( UWORD Xpoint,       UWORD Ypoint, UWORD Color,
        DOT_PIXEL Dot_Pixel,DOT_STYLE Dot_FillWay)
{
    if (Xpoint > Paint.Width || Ypoint > Paint.Height) {
        Debug("Paint_DrawPoint Input exceeds the normal display range\r\n");
        return;
    }

    int16_t XDir_Num , YDir_Num;
    if (Dot_FillWay == DOT_FILL_AROUND) {
        for (XDir_Num = 0; XDir_Num < 2*Dot_Pixel - 1; XDir_Num++) {
            for (YDir_Num = 0; YDir_Num < 2 * Dot_Pixel - 1; YDir_Num++) {
                if((int)(Xpoint + XDir_Num - Dot_Pixel) < 0 || (int)(Ypoint + YDir_Num - Dot_Pixel) < 0)
                    break;
                // printf("x = %d, y = %d\r\n", Xpoint + XDir_Num - Dot_Pixel, Ypoint + YDir_Num - Dot_Pixel);
                Paint_SetPixel(Xpoint + XDir_Num - Dot_Pixel, Ypoint + YDir_Num - Dot_Pixel, Color);
            }
        }
    } else {
        for (XDir_Num = 0; XDir_Num <  Dot_Pixel; XDir_Num++) {
            for (YDir_Num = 0; YDir_Num <  Dot_Pixel; YDir_Num++) {
                Paint_SetPixel(Xpoint + XDir_Num - 1, Ypoint + YDir_Num - 1, Color);
            }
        }
    }
}

/******************************************************************************
function:	Draw a line of arbitrary slope
parameter:
Xstart ：Starting Xpoint point coordinates
Ystart ：Starting Xpoint point coordinates
Xend   ：End point Xpoint coordinate
Yend   ：End point Ypoint coordinate
Color  ：The color of the line segment
 ******************************************************************************/
void Paint_DrawLine(UWORD Xstart, UWORD Ystart, UWORD Xend, UWORD Yend, 
        UWORD Color, DOT_PIXEL Line_width, LINE_STYLE Line_Style)
{
    if (Xstart > Paint.Width || Ystart > Paint.Height ||
            Xend > Paint.Width || Yend > Paint.Height) {
        Debug("Paint_DrawLine Input exceeds the normal display range\r\n");
        return;
    }

    UWORD Xpoint = Xstart;
    UWORD Ypoint = Ystart;
    int dx = (int)Xend - (int)Xstart >= 0 ? Xend - Xstart : Xstart - Xend;
    int dy = (int)Yend - (int)Ystart <= 0 ? Yend - Ystart : Ystart - Yend;

    // Increment direction, 1 is positive, -1 is counter;
    int XAddway = Xstart < Xend ? 1 : -1;
    int YAddway = Ystart < Yend ? 1 : -1;

    //Cumulative error
    int Esp = dx + dy;
    char Dotted_Len = 0;

    for (;;) {
        Dotted_Len++;
        //Painted dotted line, 2 point is really virtual
        if (Line_Style == LINE_STYLE_DOTTED && Dotted_Len % 3 == 0) {
            //Debug("LINE_DOTTED\r\n");
            Paint_DrawPoint(Xpoint, Ypoint, IMAGE_BACKGROUND, Line_width, DOT_STYLE_DFT);
            Dotted_Len = 0;
        } else {
            Paint_DrawPoint(Xpoint, Ypoint, Color, Line_width, DOT_STYLE_DFT);
        }
        if (2 * Esp >= dy) {
            if (Xpoint == Xend)
                break;
            Esp += dy;
            Xpoint += XAddway;
        }
        if (2 * Esp <= dx) {
            if (Ypoint == Yend)
                break;
            Esp += dx;
            Ypoint += YAddway;
        }
    }
}

/******************************************************************************
function:	Draw a rectangle
parameter:
Xstart ：Rectangular  Starting Xpoint point coordinates
Ystart ：Rectangular  Starting Xpoint point coordinates
Xend   ：Rectangular  End point Xpoint coordinate
Yend   ：Rectangular  End point Ypoint coordinate
Color  ：The color of the Rectangular segment
Filled : Whether it is filled--- 1 solid 0：empty
 ******************************************************************************/
void Paint_DrawRectangle( UWORD Xstart, UWORD Ystart, UWORD Xend, UWORD Yend, 
        UWORD Color, DOT_PIXEL Line_width, DRAW_FILL Filled )
{
    if (Xstart > Paint.Width || Ystart > Paint.Height ||
            Xend > Paint.Width || Yend > Paint.Height) {
        Debug("Input exceeds the normal display range\r\n");
        return;
    }

    if (Filled ) {
        UWORD Ypoint;
        for(Ypoint = Ystart; Ypoint < Yend; Ypoint++) {
            Paint_DrawLine(Xstart, Ypoint, Xend, Ypoint, Color ,Line_width, LINE_STYLE_SOLID);
        }
    } else {
        Paint_DrawLine(Xstart, Ystart, Xend, Ystart, Color ,Line_width, LINE_STYLE_SOLID);
        Paint_DrawLine(Xstart, Ystart, Xstart, Yend, Color ,Line_width, LINE_STYLE_SOLID);
        Paint_DrawLine(Xend, Yend, Xend, Ystart, Color ,Line_width, LINE_STYLE_SOLID);
        Paint_DrawLine(Xend, Yend, Xstart, Yend, Color ,Line_width, LINE_STYLE_SOLID);
    }
}

/******************************************************************************
function:	Use the 8-point method to draw a circle of the
specified size at the specified position->
parameter:
X_Center  ：Center X coordinate
Y_Center  ：Center Y coordinate
Radius    ：circle Radius
Color     ：The color of the ：circle segment
Filled    : Whether it is filled: 1 filling 0：Do not
 ******************************************************************************/
void Paint_DrawCircle(  UWORD X_Center, UWORD Y_Center, UWORD Radius, 
        UWORD Color, DOT_PIXEL Line_width, DRAW_FILL Draw_Fill )
{
    if (X_Center > Paint.Width || Y_Center >= Paint.Height) {
        Debug("Paint_DrawCircle Input exceeds the normal display range\r\n");
        return;
    }

    //Draw a circle from(0, R) as a starting point
    int16_t XCurrent, YCurrent;
    XCurrent = 0;
    YCurrent = Radius;

    //Cumulative error,judge the next point of the logo
    int16_t Esp = 3 - (Radius << 1 );

    int16_t sCountY;
    if (Draw_Fill == DRAW_FILL_FULL) {
        while (XCurrent <= YCurrent ) { //Realistic circles
            for (sCountY = XCurrent; sCountY <= YCurrent; sCountY ++ ) {
                Paint_DrawPoint(X_Center + XCurrent, Y_Center + sCountY, Color, DOT_PIXEL_DFT, DOT_STYLE_DFT);//1
                Paint_DrawPoint(X_Center - XCurrent, Y_Center + sCountY, Color, DOT_PIXEL_DFT, DOT_STYLE_DFT);//2
                Paint_DrawPoint(X_Center - sCountY, Y_Center + XCurrent, Color, DOT_PIXEL_DFT, DOT_STYLE_DFT);//3
                Paint_DrawPoint(X_Center - sCountY, Y_Center - XCurrent, Color, DOT_PIXEL_DFT, DOT_STYLE_DFT);//4
                Paint_DrawPoint(X_Center - XCurrent, Y_Center - sCountY, Color, DOT_PIXEL_DFT, DOT_STYLE_DFT);//5
                Paint_DrawPoint(X_Center + XCurrent, Y_Center - sCountY, Color, DOT_PIXEL_DFT, DOT_STYLE_DFT);//6
                Paint_DrawPoint(X_Center + sCountY, Y_Center - XCurrent, Color, DOT_PIXEL_DFT, DOT_STYLE_DFT);//7
                Paint_DrawPoint(X_Center + sCountY, Y_Center + XCurrent, Color, DOT_PIXEL_DFT, DOT_STYLE_DFT);
            }
            if (Esp < 0 )
                Esp += 4 * XCurrent + 6;
            else {
                Esp += 10 + 4 * (XCurrent - YCurrent );
                YCurrent --;
            }
            XCurrent ++;
        }
    } else { //Draw a hollow circle
        while (XCurrent <= YCurrent ) {
            Paint_DrawPoint(X_Center + XCurrent, Y_Center + YCurrent, Color, Line_width, DOT_STYLE_DFT);//1
            Paint_DrawPoint(X_Center - XCurrent, Y_Center + YCurrent, Color, Line_width, DOT_STYLE_DFT);//2
            Paint_DrawPoint(X_Center - YCurrent, Y_Center + XCurrent, Color, Line_width, DOT_STYLE_DFT);//3
            Paint_DrawPoint(X_Center - YCurrent, Y_Center - XCurrent, Color, Line_width, DOT_STYLE_DFT);//4
            Paint_DrawPoint(X_Center - XCurrent, Y_Center - YCurrent, Color, Line_width, DOT_STYLE_DFT);//5
            Paint_DrawPoint(X_Center + XCurrent, Y_Center - YCurrent, Color, Line_width, DOT_STYLE_DFT);//6
            Paint_DrawPoint(X_Center + YCurrent, Y_Center - XCurrent, Color, Line_width, DOT_STYLE_DFT);//7
            Paint_DrawPoint(X_Center + YCurrent, Y_Center + XCurrent, Color, Line_width, DOT_STYLE_DFT);//0

            if (Esp < 0 )
                Esp += 4 * XCurrent + 6;
            else {
                Esp += 10 + 4 * (XCurrent - YCurrent );
                YCurrent --;
            }
            XCurrent ++;
        }
    }
}

/******************************************************************************
function: Show English characters
parameter:
Xpoint           ：X coordinate
Ypoint           ：Y coordinate
Acsii_Char       ：To display the English characters
Font             ：A structure pointer that displays a character size
Color_Background : Select the background color of the English character
Color_Foreground : Select the foreground color of the English character
 ******************************************************************************/
void Paint_DrawChar(UWORD Xpoint, UWORD Ypoint, const char Acsii_Char,
        sFONT* Font, UWORD Color_Background, UWORD Color_Foreground)
{

    //static int cnt = 0;//for debug
    UWORD Page, Column;

    if (Xpoint > Paint.Width || Ypoint > Paint.Height) {
        //Debug("Paint_DrawChar Input exceeds the normal display range\r\n");
        return;
    }
    uint32_t Char_Offset = (Acsii_Char - ' ') * Font->Height * (Font->Width / 8 + (Font->Width % 8 ? 1 : 0));
    const unsigned char *ptr = &Font->table[Char_Offset];

    if (pChar_buf == NULL) {
        if (!init_char_buf()) {
            ESP_LOGE("MEMALLOC", "Falied!");
            return;
        }
    }
    uint16_t *wPtr = (uint16_t *) pChar_buf;
    uint16_t fc = Color_Foreground;
    uint16_t bc = Color_Background;
    for ( Page = 0; Page < Font->Height; Page ++ ) {
        for ( Column = 0; Column < Font->Width; Column ++ ) {
            uint32_t n = Page * Font->Width + Column;
            wPtr[n] = ((*ptr) & (0x80 >> (Column % 8))) ? fc : bc;
            if (Column % 8 == 7) {
                ptr++;
            }
        }
        if (Font->Width % 8 != 0) {
            ptr++;
        }
    }
    wPtr = (uint16_t *) pChar_buf;
    paint_rect(Xpoint, Xpoint + Font->Width, Ypoint, Ypoint + Font->Height, wPtr);
}

/******************************************************************************
function: Display the string
parameter:
Xstart           ：X coordinate
Ystart           ：Y coordinate
pString          ：The first address of the English string to be displayed
Font             ：A structure pointer that displays a character size
Color_Background : Select the background color of the English character
Color_Foreground : Select the foreground color of the English character
 ******************************************************************************/
void Paint_DrawString_EN(UWORD Xstart, UWORD Ystart, const char * pString,
        sFONT* Font, UWORD Color_Background, UWORD Color_Foreground )
{
    UWORD Xpoint = Xstart;
    UWORD Ypoint = Ystart;

    if (Xstart > Paint.Width || Ystart > Paint.Height) {
        //Debug("Paint_DrawString_EN Input exceeds the normal display range\r\n");
        return;
    }

    static int cnt = 0;
    while (* pString != '\0') {
        //if X direction filled , reposition to(Xstart,Ypoint),Ypoint is Y direction plus the Height of the character
        if ((Xpoint + Font->Width ) > Paint.Width ) {
            Xpoint = Xstart;
            Ypoint += Font->Height;
        }

        // If the Y direction is full, reposition to(Xstart, Ystart)
        if ((Ypoint  + Font->Height ) > Paint.Height ) {
            Xpoint = Xstart;
            Ypoint = Ystart;
        }
        //if (cnt==0) LCD_ClearWindow(Xpoint, Ypoint, Xpoint+Font->Width-1, Ypoint+Font->Height-1, BLUE);
        //else if (cnt==1) LCD_ClearWindow(Xpoint, Ypoint, Xpoint+Font->Width-1, Ypoint+Font->Height-1, GREEN);
        //else Paint_DrawChar(Xpoint, Ypoint, * pString, Font, Color_Background, Color_Foreground);
        Paint_DrawChar(Xpoint, Ypoint, * pString, Font, Color_Background, Color_Foreground);

        //The next character of the address
        pString ++;

        //The next word of the abscissa increases the font of the broadband
        Xpoint += Font->Width;
        cnt++;
    }
}


/******************************************************************************
function: Display the string
parameter:
Xstart           ：X coordinate
Ystart           ：Y coordinate
pString          ：The first address of the Chinese string and English
string to be displayed
Font             ：A structure pointer that displays a character size
Color_Background : Select the background color of the English character
Color_Foreground : Select the foreground color of the English character
 ******************************************************************************/
void Paint_DrawString_CN(UWORD Xstart, UWORD Ystart, const char * pString, cFONT* font, UWORD Color_Background, UWORD Color_Foreground)
{
    const char* p_text = pString;

    int refcolumn = Xstart;
    int i, j, Num;
    /* Send the string character by character on EPD */
    while (*p_text != 0) {
        if (*p_text < 0x7F) {                                  //ASCII
            for (Num = 0; Num < font->size ; Num++) {
                if (*p_text == font->table[Num].index[0]) {
                    const char* ptr = &font->table[Num].matrix[0];

                    for (j = 0; j < font->Height; j++) {
                        for (i = 0; i < font->Width; i++) {
                            if ((*ptr) & (0x80 >> (i % 8))) {
                                Paint_SetPixel(refcolumn + i,Ystart + j, Color_Foreground);
                            }
                            if (i % 8 == 7) {
                                ptr++;
                            }
                        }
                        if (font->Width % 8 != 0) {
                            ptr++;
                        }
                    }
                    break;
                }
            }
            /* Point on the next character */
            p_text += 1;
            /* Decrement the column position by 16 */
            refcolumn += font->ASCII_Width;
        } else {                                   //中文
            for (Num = 0; Num < font->size ; Num++) {
                if ((*p_text == font->table[Num].index[0]) && (*(p_text + 1) == font->table[Num].index[1]) && (*(p_text + 2) == font->table[Num].index[2])) {
                    const char* ptr = &font->table[Num].matrix[0];

                    for (j = 0; j < font->Height; j++) {
                        for (i = 0; i < font->Width; i++) {
                            if ((*ptr) & (0x80 >> (i % 8))) {
                                Paint_SetPixel(refcolumn + i,Ystart + j, Color_Foreground);
                            }
                            if (i % 8 == 7) {
                                ptr++;
                            }
                        }
                        if (font->Width % 8 != 0) {
                            ptr++;
                        }
                    }
                    break;
                }
            }
            /* Point on the next character */
            p_text += 3;
            /* Decrement the column position by 16 */
            refcolumn += font->Width;
        }
    }
}

/******************************************************************************
function: Display nummber
parameter:
Xstart           ：X coordinate
Ystart           : Y coordinate
Nummber          : The number displayed
Font             ：A structure pointer that displays a character size
Color_Background : Select the background color of the English character
Color_Foreground : Select the foreground color of the English character
 ******************************************************************************/
#define  ARRAY_LEN 50
void Paint_DrawNum(UWORD Xpoint, UWORD Ypoint, int32_t Nummber,
        sFONT* Font, UWORD Color_Background, UWORD Color_Foreground )
{

    int16_t Num_Bit = 0, Str_Bit = 0;
    uint8_t Str_Array[ARRAY_LEN] = {0}, Num_Array[ARRAY_LEN] = {0};
    uint8_t *pStr = Str_Array;

    if (Xpoint > Paint.Width || Ypoint > Paint.Height) {
        //Debug("Paint_DisNum Input exceeds the normal display range\r\n");
        return;
    }

    //Converts a number to a string
    do{
        Num_Array[Num_Bit] = Nummber % 10 + '0';
        Num_Bit++;
        Nummber /= 10;
    }while (Nummber);

    //The string is inverted
    while (Num_Bit > 0) {
        Str_Array[Str_Bit] = Num_Array[Num_Bit - 1];
        Str_Bit ++;
        Num_Bit --;
    }

    //show
    Paint_DrawString_EN(Xpoint, Ypoint, (const char*)pStr, Font, Color_Background, Color_Foreground);
}
/******************************************************************************
function:	Display float number
parameter:
Xstart           ：X coordinate
Ystart           : Y coordinate
Nummber          : The float data that you want to display
Decimal_Point	 : Show decimal places
Font             ：A structure pointer that displays a character size
Color            : Select the background color of the English character
 ******************************************************************************/
void Paint_DrawFloatNum(UWORD Xpoint, UWORD Ypoint, double Nummber,  UBYTE Decimal_Point, 
        sFONT* Font,  UWORD Color_Background, UWORD Color_Foreground)
{
    char Str[ARRAY_LEN] = {0};
    //dtostrf(Nummber,0,Decimal_Point+2,Str);
    sprintf(Str, "%.*f", Decimal_Point + 2, Nummber);
    char * pStr= (char *)malloc((strlen(Str))*sizeof(char));
    memcpy(pStr,Str,(strlen(Str)-2));
    * (pStr+strlen(Str)-1)='\0';
    if((*(pStr+strlen(Str)-3))=='.')
    {
        *(pStr+strlen(Str)-3)='\0';
    }
    //show
    Paint_DrawString_EN(Xpoint, Ypoint, (const char*)pStr, Font, Color_Foreground, Color_Background);
    free(pStr);
    pStr=NULL;
}

/******************************************************************************
function: Display time
parameter:
Xstart           ：X coordinate
Ystart           : Y coordinate
pTime            : Time-related structures
Font             ：A structure pointer that displays a character size
Color            : Select the background color of the English character
 ******************************************************************************/
void Paint_DrawTime(UWORD Xstart, UWORD Ystart, PAINT_TIME *pTime, sFONT* Font,
        UWORD Color_Background, UWORD Color_Foreground)
{
    uint8_t value[10] = {'0', '1', '2', '3', '4', '5', '6', '7', '8', '9'};

    UWORD Dx = Font->Width;

    //Write data into the cache
    Paint_DrawChar(Xstart                           , Ystart, value[pTime->Hour / 10], Font, Color_Background, Color_Foreground);
    Paint_DrawChar(Xstart + Dx                      , Ystart, value[pTime->Hour % 10], Font, Color_Background, Color_Foreground);
    Paint_DrawChar(Xstart + Dx  + Dx / 4 + Dx / 2   , Ystart, ':'                    , Font, Color_Background, Color_Foreground);
    Paint_DrawChar(Xstart + Dx * 2 + Dx / 2         , Ystart, value[pTime->Min / 10] , Font, Color_Background, Color_Foreground);
    Paint_DrawChar(Xstart + Dx * 3 + Dx / 2         , Ystart, value[pTime->Min % 10] , Font, Color_Background, Color_Foreground);
    Paint_DrawChar(Xstart + Dx * 4 + Dx / 2 - Dx / 4, Ystart, ':'                    , Font, Color_Background, Color_Foreground);
    Paint_DrawChar(Xstart + Dx * 5                  , Ystart, value[pTime->Sec / 10] , Font, Color_Background, Color_Foreground);
    Paint_DrawChar(Xstart + Dx * 6                  , Ystart, value[pTime->Sec % 10] , Font, Color_Background, Color_Foreground);
}

/******************************************************************************
function: Display image
parameter:
image            ：Image start address
xStart           : X starting coordinates
yStart           : Y starting coordinates
xEnd             ：Image width
yEnd             : Image height
 ******************************************************************************/
void Paint_DrawImage(const unsigned char *image, UWORD xStart, UWORD yStart, UWORD W_Image, UWORD H_Image)
{
    int i, j;
    for (j = 0; j < H_Image; j++) {
        for (i = 0; i < W_Image; i++) {
            if (xStart + i < LCD_WIDTH  &&  yStart + j < LCD_HEIGHT) //Exceeded part does not display
                Paint_SetPixel(xStart + i, yStart + j, (*(image + j * W_Image * 2 + i * 2 + 1)) << 8 | (*(image + j * W_Image * 2 + i * 2)));
            //Using arrays is a property of sequential storage, accessing the original array by algorithm
            //j*W_Image*2          Y offset
            //i*2                  X offset
        }
    }

}
