set(sources
    main.c
    storage.c
    ice40up5k/ice.c
    network/network.c
    network/network_adapter.c
    network/network_mngr.c
    network/web_server.c
    network/network_mngr_ota.c
    network/descriptors.c
    network/uart_websocket.c
)

set(dependencies
    fatfs
    driver
    nvs_flash
    esp_netif
    console
    esptool_py
    esp_gdbstub
    esp_psram
    esp_http_server
    esp_http_client
    esp_https_ota
    esp_wifi
    app_update
    wifi_provisioning
    json
    compat
    platform_include
    blackmagic_esp32
    lcd
    CherryDAP
)

idf_component_register(
    SRCS
        ${sources}
    INCLUDE_DIRS
        . network ice40up5k
    PRIV_REQUIRES
        ${dependencies}
        esp_https_server
    EMBED_FILES
        "web/website.html"
        "web/logic_analyzer.html"
        "web/help.html"
        "web/ez32.svg"
        "web/favicon.ico"
        "ice40up5k/bitstream.bin"
        "cacert.pem"
        "prvtkey.pem"
)

set(host "esp-idf")
set(PLATFORM_INCLUDE_DIR ${CMAKE_SOURCE_DIR}/components/platform_include)

