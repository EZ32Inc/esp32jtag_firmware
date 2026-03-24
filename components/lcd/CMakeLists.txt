idf_component_register(
        SRCS
        "DEV_Config.c"
#       "font12.c"    # not needed at runtime; kept for reference
        "font16.c"
        "font20.c"
        "font24.c"
#       "font24CN.c"  # Chinese font; not needed at runtime; kept for reference
        "font8.c"
        "GUI_Paint.c"
        "image.c"
        "LCD_1in83.c"
        "LCD_Driver.c"
        "jtag.c"
#        "gowin_esp32.c"
        "part.c"
        "char_buf.c"
        "lcd_library.c"
        "storage.c"
        "xvc_server.c"
        "pack_jtag_data.c"
        INCLUDE_DIRS "../" "." "../blackmagic_esp32/src/include" "../blackmagic_esp32/src/platforms/esp32/main/" "../../main/"
        REQUIRES driver esp_wifi nvs_flash esp_timer esp_lcd esp_http_server json fatfs esp_adc esp_http_client
        )
