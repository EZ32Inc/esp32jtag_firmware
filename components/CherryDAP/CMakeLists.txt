idf_component_register(
    SRCS
        "dap_main.c"
        "CherryRB/chry_ringbuffer.c"
        "CherryUSB/core/usbd_core.c"
        "CherryUSB/class/cdc/usbd_cdc_acm.c"
        "CherryUSB/class/msc/usbd_msc.c"
        "CherryUSB/port/dwc2/usb_glue_esp.c"
        "CherryUSB/port/dwc2/usb_dc_dwc2.c"
        "DAP/Source/DAP.c"
        "DAP/Source/DAP_vendor.c"
        "DAP/Source/JTAG_DP.c"
        "DAP/Source/SWO.c"
        "DAP/Source/SW_DP.c"
        "DAP/Source/UART.c"
        "projects/esp32s3/main/usb2uart.c"
    INCLUDE_DIRS
        "."
        "CherryRB"
        "CherryUSB/core"
        "CherryUSB/common"
        "CherryUSB/class/cdc"
        "CherryUSB/class/msc"
        "CherryUSB/class/hid"
        "CherryUSB/class/hub"
        "DAP/Include"
        "projects/esp32s3/main"
    REQUIRES
        "esp_http_client"
        "driver"
        "usb"
        "nvs_flash"
        "freertos"
)

target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-unused-const-variable -Wno-format -Wno-format-truncation -Wno-incompatible-pointer-types -Wno-type-limits)

target_compile_definitions(${COMPONENT_LIB} PUBLIC
    CONFIG_USBDEV_ADVANCE_DESC=1
    CONFIG_CHERRYUSB_DEVICE_DCD="dwc2_esp"
)
