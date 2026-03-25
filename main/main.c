#include "sdkconfig.h"

#include <unistd.h>

#include "esp_log.h"
#include "esp_system.h"
#include "esp_mac.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "nvs_flash.h"
#include "esp_check.h"
#include "driver/uart.h"
#include "esp_timer.h"
#include "driver/ledc.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_http_client.h"
#include "esp_http_server.h"
#include "freertos/semphr.h"
#include "esp_wifi.h"
#include "driver/gpio.h"

#include "types.h"
#include "storage.h"
#include "network.h"
#include "descriptors.h"
#include "network_mngr.h"
#include "network_mngr_ota.h"
#include "web_server.h"
#include "uart_websocket.h"
#include "../components/lcd/lcd_library.h"
#include "GUI_Paint.h"
#include "gdb_main.h"
#include "port_cfg.h"
#include "xvc_server.h"
#include "esp32jtag_common.h"
#include "ice.h"
#include "dap_main.h"
#include "usb_config.h"
#include "chry_ringbuffer.h"

#include <stdio.h>
#include <stdarg.h>

//in ../components/CherryDAP/dap_main.c
int usb_cdc_log_vprintf(const char *format, va_list args);
#define usb_log_vprintf usb_cdc_log_vprintf

extern void uartx_preinit(void);
extern void uart_event_task(void *pvParameters);

static void cherry_dap_task(void *arg) {
    if (g_app_params.uart_port_sel == 0) {
        uartx_preinit();
    }
    while (1) {
        chry_dap_handle();
        if (g_app_params.uart_port_sel == 0) {
            uart_event_task(NULL);
            chry_dap_usb2uart_handle();
        }
        // ESP_LOGI("DAP", "cherry_dap_task");
        vTaskDelay(1); // Optional yield if needed, but handle might be blocking or polling
    }
}

//assign {SWD_GPIO, LA_input_sel, cfgpd, cfgpb, cfgpa, njtag_swdio, sreset} = data_reg_0[6:0];  //from nvme1t/work/esp32jtag_v1d3_ice4kup/top.v

uint8_t global_data_reg_0 = 0; //for port configuration and sreset, njtag_swdio
uint8_t global_data_reg_1 = 0; //bit 7 to set wr_and_rd or read only, bit 6 to 0 to set PC and PD output enable and signal types

extern void platform_init(void); //from ./components/blackmagic_esp32
extern void logic_analyzer_init();

//global:
SemaphoreHandle_t capture_start_semaphore;
SemaphoreHandle_t capture_done_semaphore;
bool gbl_capture_started = false;
bool gbl_triggered_flag = true;
bool gbl_all_captured_flag = true;
uint32_t gbl_wr_addr_stop_position = 0;
uint32_t gbl_trigger_position = 0;

uint8_t *gbl_spi_rxbuf = NULL;

#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 3, 0)
#include "driver/uart_vfs.h"
#define vfs_dev_port_set_rx_line_endings uart_vfs_dev_port_set_rx_line_endings
#define vfs_dev_port_set_tx_line_endings uart_vfs_dev_port_set_tx_line_endings
#define vfs_dev_uart_use_driver uart_vfs_dev_use_driver
#else
#include "esp_vfs_dev.h"
#define vfs_dev_port_set_rx_line_endings esp_vfs_dev_uart_port_set_rx_line_endings
#define vfs_dev_port_set_tx_line_endings esp_vfs_dev_uart_port_set_tx_line_endings
#define vfs_dev_uart_use_driver esp_vfs_dev_uart_use_driver
#endif

static const char *TAG = "MAIN";


//configurations for each ESP32JTAG 4 ports
uint8_t gbl_pa_cfg = 0;
uint8_t gbl_pb_cfg = 0;
uint8_t gbl_pc_cfg = 0;
uint8_t gbl_pd_cfg = 0;

app_params_t g_app_params;

adc_oneshot_unit_handle_t gbl_adc_handle = NULL;
adc_cali_handle_t adc1_cali_chan0_handle = NULL;

//components/blackmagic_esp32/src/platforms/esp32/main/gdb_if.c:void set_gdb_socket(int socket) 

extern void set_gdb_socket(int socket);
extern void set_gdb_listen(int socket);
unsigned short gbl_gdb_port = 4242; //same as stlink st-util GDB

extern volatile bool config_received;

spi_device_handle_t gbl_spi_h1 = NULL;

esp_err_t gpio_out_init(gpio_num_t gpio_num, uint32_t init_level)
{
    // 1. Configure the GPIO pin
    gpio_config_t io_conf = {};
    // Disable interrupt
    io_conf.intr_type = GPIO_INTR_DISABLE;
    // Set as output mode
    io_conf.mode = GPIO_MODE_OUTPUT;
    // Bit mask for the pins (only gpio_num)
    io_conf.pin_bit_mask = (1ULL << gpio_num);
    // Disable pull-down mode
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    // Disable pull-up mode
    io_conf.pull_up_en = GPIO_PULLUP_DISABLE;

    // Configure the GPIO with the settings
    gpio_config(&io_conf);
    
    // 2. Set the GPIO state to HIGH (3.3V)
    esp_err_t err = gpio_set_level(gpio_num, init_level);
    
    if (err == ESP_OK) {
        // Log success
        ESP_LOGI("GPIO_INIT", "GPIO %d initialized and set to %d successfully.", gpio_num, init_level);
    } else {
        // Log error if setting failed
        ESP_LOGE("GPIO_INIT", "Failed to init and set GPIO. Error code: %d", gpio_num, err);
    }
    return err;
    
}

static void init_gpio_sw1_sw2(void)
{
    ESP_LOGI(TAG, "Init GPIO0 and GPIO48 as input"); //, PIN_PUSHBUTTON_BOOT_SW1, PIN_PUSHBUTTON_SW2);
    gpio_config_t io_conf = {
        .mode = GPIO_MODE_INPUT,
        .pin_bit_mask = (1ULL << PIN_PUSHBUTTON_BOOT_SW1) | (1ULL << PIN_PUSHBUTTON_SW2) ,
        .pull_down_en = 0,
        .pull_up_en = 0,
        .intr_type = GPIO_INTR_DISABLE
    };
    gpio_config(&io_conf);
}

static esp_err_t load_fpga(void)
{
    extern const unsigned char bitstream_bin_start[] asm("_binary_bitstream_bin_start");
    extern const unsigned char bitstream_bin_end[]   asm("_binary_bitstream_bin_end");
    const size_t sz = (bitstream_bin_end - bitstream_bin_start);

    ESP_LOGI(TAG, "Configuring FPGA, bin file size=%ld", sz);

    uint8_t cfg_stat;
    int8_t retry = 3;
    while ((cfg_stat = ICE_FPGA_Config(bitstream_bin_start, sz)) && (--retry)) {
        ESP_LOGW(TAG, "FPGA configured ERROR - status = %d retry=%d", cfg_stat, retry);
    }
    if (retry)
        ESP_LOGI(TAG, "FPGA configured OK - status = %d retry=%d", cfg_stat, retry);
    else
        ESP_LOGW(TAG, "FPGA configured ERROR - giving up");

    return ESP_OK;
}

#define SPI_MAX_TRANSFER_BYTES  4096
#define SPI_CLK_MHZ             (20*1000*1000)
#define INITIAL_VIO_DUTY        8   /* 8/1024 → ~3.30V output */

/* PWM duty values for Target IO Voltage UI options (index matches HTML option value):
 *   0 = 3.3V → duty   8
 *   1 = 2.5V → duty 123
 *   2 = 1.8V → duty 228
 *   3 = 1.5V → duty 271
 *   4 = 1.2V → duty 318
 * Derived from empirical measurements on ESP32JTAG v1.3/v1.4 hardware.
 * Default (fresh flash / factory reset) is index 0 = 3.3V. */
static const uint16_t vio_duty_table[] = { 8, 123, 228, 271, 318 };
#define VIO_DUTY_TABLE_SIZE  (sizeof(vio_duty_table) / sizeof(vio_duty_table[0]))

static spi_device_handle_t spi_device_1_manual_handle;
static spi_device_handle_t spi_device_2_hw_handle;
static spi_device_handle_t spi_device_3_hw_handle;
static spi_device_handle_t spi_device_4_hw_handle;
esp_err_t spi_master_init(void){
    ESP_LOGI(TAG, "Doing spi_master_init()");


    // 1. Bus configuration (same for all devices on this bus)
    spi_bus_config_t buscfg = {
        .mosi_io_num = PIN_NUM_MOSI,
        .miso_io_num = PIN_NUM_MISO,
        .sclk_io_num = PIN_NUM_CLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = SPI_MAX_TRANSFER_BYTES
    };

    // Initialize the bus
    //esp_err_t ret = spi_bus_initialize(SPI_HOST_USED, &buscfg, SPI_DMA_CH_AUTO);
    esp_err_t ret = spi_bus_initialize(XVC_SPI_HOST, &buscfg, SPI_DMA_CH_AUTO);
    //esp_err_t ret = spi_bus_initialize(XVC_SPI_HOST, &buscfg, SPI_DMA_DISABLED);
    if (ret != ESP_OK){
        return ret;
    }

    // 2. Device 1 configuration with manual CS control
    spi_device_interface_config_t devcfg_1_manual = {
        //.clock_speed_hz = SPI_CLK_MHZ,
#if USING_ARTIX7_BOARD
        .clock_speed_hz = 26*1000*1000, //
#else
        .clock_speed_hz = 40*1000*1000, //
#endif
        .mode = 0,
        .spics_io_num = -1, // Use -1 for manual control
        //.spics_io_num = PIN_NUM_CS0, // Let the driver control this CS pin
        .queue_size = 7,
    };

    // Add Device 1 to the bus, getting its unique handle
    //ret = spi_bus_add_device(XVC_SPI_HOST, &devcfg_1_manual, &spi_device_1_manual_handle);
    ret = spi_bus_add_device(SPI_HOST_USED, &devcfg_1_manual, &spi_device_1_manual_handle);//using SPI_HOST_USED!!! Same as LCD
    if (ret != ESP_OK){
        ESP_LOGE(TAG, "spi_bus_add_device devcfg_1_manual failed");
        return ret;
    }
    gbl_spi_h1 = spi_device_1_manual_handle; //spi device for LA capture

    // 3. Device 2 configuration with hardware CS control
    spi_device_interface_config_t devcfg_2_hw = {
        .clock_speed_hz = SPI_CLK_MHZ,
        .mode = 0,
        .spics_io_num = PIN_NUM_CS1, // Let the driver control this CS pin
        .queue_size = 7,
    };

    // Add Device 2 to the bus, getting its unique handle
    ret = spi_bus_add_device(XVC_SPI_HOST, &devcfg_2_hw, &spi_device_2_hw_handle);
    if (ret != ESP_OK){
        ESP_LOGE(TAG, "spi_bus_add_device devcfg_2_hw failed");
        return ret;
    }

    // 4. Device 3 configuration with hardware CS control
    spi_device_interface_config_t devcfg_3_hw = {
        .clock_speed_hz = SPI_CLK_MHZ,
        .mode = 0,
        .spics_io_num = PIN_NUM_CS2, // Let the driver control this CS pin
        .queue_size = 7,
    };

    // Add Device 3 to the bus, getting its unique handle
    ret = spi_bus_add_device(XVC_SPI_HOST, &devcfg_3_hw, &spi_device_3_hw_handle);
    if (ret != ESP_OK){
        ESP_LOGE(TAG, "spi_bus_add_device devcfg_3_hw failed");
        return ret;
    }
    ESP_LOGI(TAG, "spi_bus_add_device devcfg_3_hw OK!");

    // 5. Device 4 configuration with hardware CS control
    spi_device_interface_config_t devcfg_4_hw = {
        .clock_speed_hz = SPI_CLK_MHZ,
        .mode = 0,
        .spics_io_num = PIN_NUM_CS3, // Let the driver control this CS pin
        .queue_size = 7,
    };

    // Add Device 4 to the bus, getting its unique handle
    ret = spi_bus_add_device(XVC_SPI_HOST, &devcfg_4_hw, &spi_device_4_hw_handle);
    if (ret != ESP_OK){
        ESP_LOGE(TAG, "spi_bus_add_device devcfg_4_hw failed");
        return ret;
    }
    ESP_LOGI(TAG, "spi_bus_add_device devcfg_4_hw OK!");

    // 6. Manually configure the CS pin for Device 1 as a GPIO output
    gpio_config_t io_conf = {};
    io_conf.intr_type = GPIO_INTR_DISABLE;
    io_conf.mode = GPIO_MODE_OUTPUT;
    io_conf.pin_bit_mask = (1ULL << PIN_NUM_CS0);
    io_conf.pull_down_en = 0;
    io_conf.pull_up_en = 0;
    gpio_config(&io_conf);
    gpio_set_level(PIN_NUM_CS0, 1); //Assert CS by default

    ESP_LOGI(TAG, "spi_master_init() done, gbl_spi_h1 is %s.", gbl_spi_h1==NULL? "NULL":"Not NULL"); 
    return ret;
}
esp_err_t spi_comm_with_device_0(const uint8_t *tx_data, uint8_t *rx_data, size_t length)
{
    spi_transaction_t t = {
        .length = length * 8,          // length in **bits**
        .tx_buffer = tx_data,
        .rx_buffer = rx_data,
    };

    gpio_set_level(PIN_NUM_CS0, 0); // Manual CS assert
    esp_err_t ret = spi_device_transmit(spi_device_1_manual_handle, &t);
    gpio_set_level(PIN_NUM_CS0, 1); // Manual CS de-assert
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "SPI transfer failed: %s", esp_err_to_name(ret));
    } else {
        //ESP_LOGI(TAG, "Transfer successful. len=%d",length);
    }
    return  ret;
}

// Define a type for the SPI device handle for clarity
typedef spi_device_handle_t spi_handle_t;

/**
 * @brief Transfers data over SPI for a specified device.
 *
 * @param device_handle The handle of the SPI device to use (e.g., spi_device_2_hw_handle).
 * @param tx_data Pointer to the transmit buffer.
 * @param rx_data Pointer to the receive buffer (or NULL if not needed).
 * @param length The length of the data in bytes.
 * @return esp_err_t ESP_OK on success, or an error code on failure.
 */
esp_err_t spi_transfer_data(spi_handle_t device_handle, const uint8_t *tx_data, uint8_t *rx_data, size_t length)
{
    // 1. Prepare the SPI transaction structure
    spi_transaction_t t = {
        // 'length' is in bytes, but the SPI function expects length in **bits**
        .length = length * 8,
        .tx_buffer = tx_data,
        .rx_buffer = rx_data,
    };

    // 2. Transmit the data using the specified device handle
    esp_err_t ret = spi_device_transmit(device_handle, &t);

    // 3. Check for errors and log if necessary
    if (ret != ESP_OK) {
        // Use a generic error log, as the device is now dynamic
        ESP_LOGE(TAG, "SPI transfer failed: %s", esp_err_to_name(ret));
    }
    // You can keep the successful transfer log commented out or remove it
    /* else {
        //ESP_LOGI(TAG, "Transfer successful. len=%d", length);
    } */

    return ret;
}

// Helper wrappers for compatibility and cleaner usage in other parts of the code
esp_err_t spi_device2_transfer_data(const uint8_t *tx_data, uint8_t *rx_data, size_t length)
{
    return spi_transfer_data(spi_device_2_hw_handle, tx_data, rx_data, length);
}

esp_err_t spi_device3_transfer_data(const uint8_t *tx_data, uint8_t *rx_data, size_t length)
{
    return spi_transfer_data(spi_device_3_hw_handle, tx_data, rx_data, length);
}

esp_err_t spi_device4_transfer_data(const uint8_t *tx_data, uint8_t *rx_data, size_t length)
{
    return spi_transfer_data(spi_device_4_hw_handle, tx_data, rx_data, length);
}
void set_gpio_high_z(gpio_num_t gpio_num)
{
    // Configure as input
    gpio_config_t io_conf = {
        .pin_bit_mask = 1ULL << gpio_num,
        .mode = GPIO_MODE_INPUT,      // Input mode = high-impedance
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    gpio_config(&io_conf);
}

extern void gdb_application_thread(void *pvParameters);


/* Read a uint8 from NVS by key. On miss, writes default and uses it. */
static void load_nvs_uint8(const char *key, uint8_t *out, uint8_t default_val)
{
    char *val = NULL;
    if (storage_alloc_and_read(key, &val) == ESP_OK && val) {
        *out = (uint8_t)atoi(val);
        free(val);
    } else {
        *out = default_val;
        char buf[4];
        snprintf(buf, sizeof(buf), "%d", default_val);
        storage_write(key, buf, strlen(buf) + 1);
        ESP_LOGI(TAG, "Saved default %s=%d to NVS", key, default_val);
    }
}

static void load_port_configurations(void)
{
    load_nvs_uint8(PORT_A_CFG_KEY, &gbl_pa_cfg, PA_LOGICANALYZER);
    load_nvs_uint8(PORT_B_CFG_KEY, &gbl_pb_cfg, PB_LOGICANALYZER);
    load_nvs_uint8(PORT_C_CFG_KEY, &gbl_pc_cfg, PC_LOGICANALYZER);
    load_nvs_uint8(PORT_D_CFG_KEY, &gbl_pd_cfg, PD_LOGICANALYZER);

    // uart_port_sel is 'char', handled separately
    char *val = NULL;
    if (storage_alloc_and_read(UART_PORT_SEL_KEY, &val) == ESP_OK && val) {
        g_app_params.uart_port_sel = (uint8_t)atoi(val);
        free(val);
    } else {
        g_app_params.uart_port_sel = 1;  /* default: web terminal */
        storage_write(UART_PORT_SEL_KEY, "1", 2);
        ESP_LOGI(TAG, "Saved default UART_PORT_SEL=1 to NVS");
    }

    ESP_LOGI(TAG, "Port A Config: %s", get_port_a_description_int(gbl_pa_cfg));
    ESP_LOGI(TAG, "Port B Config: %s", get_port_b_description_int(gbl_pb_cfg));
    ESP_LOGI(TAG, "Port C Config: %s", get_port_c_description_int(gbl_pc_cfg));
    ESP_LOGI(TAG, "Port D Config: %s", get_port_d_description_int(gbl_pd_cfg));
}

/* Load AP SSID and password from NVS, generating a unique SSID on first boot. */
static void load_ap_params(void)
{
    char *ap_ssid = NULL;
    if (storage_alloc_and_read(WIFI_AP_SSID_KEY, &ap_ssid) == ESP_OK && ap_ssid) {
        strncpy(g_app_params.wifi_ssid, ap_ssid, sizeof(g_app_params.wifi_ssid) - 1);
        g_app_params.wifi_ssid[sizeof(g_app_params.wifi_ssid) - 1] = '\0';
        free(ap_ssid);
    } else {
        // Fallback to default with MAC suffix for uniqueness
        uint8_t mac[6];
        esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP);
        char unique_ssid[32];
        snprintf(unique_ssid, sizeof(unique_ssid), "%s_%02X%02X", WIFI_AP_SSSID, mac[4], mac[5]);
        strcpy(g_app_params.wifi_ssid, unique_ssid);
        storage_write(WIFI_AP_SSID_KEY, unique_ssid, strlen(unique_ssid));
        ESP_LOGI(TAG, "Saved default Unique AP SSID to NVS: %s", unique_ssid);
    }

    char *ap_pass = NULL;
    if (storage_alloc_and_read(WIFI_AP_PASS_KEY, &ap_pass) == ESP_OK && ap_pass) {
        strncpy(g_app_params.wifi_pass, ap_pass, sizeof(g_app_params.wifi_pass) - 1);
        g_app_params.wifi_pass[sizeof(g_app_params.wifi_pass) - 1] = '\0';
        free(ap_pass);
    } else {
        strcpy(g_app_params.wifi_pass, WIFI_AP_PASSW);
        storage_write(WIFI_AP_PASS_KEY, WIFI_AP_PASSW, strlen(WIFI_AP_PASSW));
        ESP_LOGI(TAG, "Saved default AP Password to NVS");
    }
}

/*
 * Load WiFi network parameters from NVS.
 * If WIFI_MODE_KEY is missing (first boot), defaults to AP mode.
 * If menuconfig has user values and NVS has no SSID, uses STA mode.
 */
static void load_network_params(void) {
    // SINGLE SOURCE OF TRUTH: Read WIFI_MODE_KEY ("SM" or "AP")
    char *wifi_mode_str = NULL;
    bool is_sta_mode = false;

    if (storage_alloc_and_read(WIFI_MODE_KEY, &wifi_mode_str) == ESP_OK && wifi_mode_str) {
        if (strcmp(wifi_mode_str, "SM") == 0) {
            is_sta_mode = true;
        }
        ESP_LOGI(TAG, "Read WIFI_MODE_KEY: %s, setting mode to %s", wifi_mode_str, is_sta_mode ? "STA" : "AP");
        free(wifi_mode_str);
    } else {
        // Default to AP if key is missing (first boot)
        ESP_LOGW(TAG, "WIFI_MODE_KEY not found. Defaulting to AP Mode and saving to NVS.");
        is_sta_mode = false;
        storage_write(WIFI_MODE_KEY, "AP", 2);
    }

    size_t ssid_len = storage_get_value_length(WIFI_SSID_KEY);
    ESP_LOGI(TAG, "ssid_len:%d", ssid_len);

    if (!is_sta_mode) {
        ESP_LOGW(TAG, "Activating AP mode!");
        g_app_params.mode = APP_MODE_AP;
        load_ap_params();
    }
    else {
        if (ssid_len == 0) {
            ESP_LOGW(TAG, "Failed to get WiFi SSID len from nvs.");
            if (!strcmp(CONFIG_ESP_WIFI_SSID, WIFI_AP_SSSID)) {
                ESP_LOGW(TAG, "Default values read from menuconfig. AP mode will be activated!");
                g_app_params.mode = APP_MODE_AP;
                strcpy(g_app_params.wifi_ssid, WIFI_AP_SSSID);
                strcpy(g_app_params.wifi_pass, WIFI_AP_PASSW);
            } else {
                ESP_LOGW(TAG, "User values read from menuconfig. STA mode will be activated!");
                g_app_params.mode = APP_MODE_STA;
                strcpy(g_app_params.wifi_ssid, CONFIG_ESP_WIFI_SSID);
                strcpy(g_app_params.wifi_pass, CONFIG_ESP_WIFI_PASSWORD);
            }
        } else {
            g_app_params.mode = APP_MODE_STA;
            esp_err_t err = storage_read(WIFI_SSID_KEY, g_app_params.wifi_ssid, ssid_len);
            if (err != ESP_OK) {
                ESP_LOGW(TAG, "Failed to read WiFi ssid");
            } else {
                size_t pass_len = storage_get_value_length(WIFI_PASS_KEY);
                if (pass_len == 0) {
                    ESP_LOGW(TAG, "Failed to get WiFi password len");
                } else {
                    err = storage_read(WIFI_PASS_KEY, g_app_params.wifi_pass, pass_len);
                    if (err != ESP_OK) {
                        ESP_LOGW(TAG, "Failed to read WiFi password");
                    }
                }
            }
        }
    }

    if (g_app_params.mode == APP_MODE_AP) {
        g_app_params.net_adapter_name = "wifiap";
    } else if (g_app_params.mode == APP_MODE_STA) {
        g_app_params.net_adapter_name = "wifista";
    }

    ESP_LOGI(TAG, "app mode (%s)", g_app_params.mode == APP_MODE_AP ? "Access Point" : "Station");
    ESP_LOGI(TAG, "wifi ssid (%s)", g_app_params.wifi_ssid);
    ESP_LOGI(TAG, "wifi pass (%s)", g_app_params.wifi_pass);
}

static void init_idf_components(void) {
    ESP_LOGI(TAG, "Setting up...\n");
    //init_console();
    ESP_ERROR_CHECK(storage_init_filesystem());
    ESP_ERROR_CHECK(nvs_flash_init());
    storage_init_nvs();
    esp_netif_init();
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    char *disable_dap_val = NULL;
    bool disable_usb_dap = true;  /* default: disabled */
    if (storage_alloc_and_read(DISABLE_USB_DAP_KEY, &disable_dap_val) == ESP_OK && disable_dap_val) {
        disable_usb_dap = (strcmp(disable_dap_val, "1") == 0);
        free(disable_dap_val);
    }

    if (!disable_usb_dap) {
        chry_dap_init(0, ESP_USBD_BASE);
    } else {
        ESP_LOGI(TAG, "Skipping chry_dap_init because USB DAP is disabled.");
    }
    // Redirect logs to USB CDC
    //esp_log_set_vprintf(usb_log_vprintf);
}

static bool get_ap_ip_str(char *buf, size_t buf_len) {
    if (buf == NULL || buf_len < 16) {
        return false;
    }

    esp_netif_ip_info_t ip_info;
    esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_AP_DEF");

    if (netif == NULL) {
        return false;
    }
    if (esp_netif_get_ip_info(netif, &ip_info) != ESP_OK) {
        return false;
    }

    snprintf(buf, buf_len, IPSTR, IP2STR(&ip_info.ip));
    return true;
}

/*--- ADC Calibration ---*/
static bool adc_calibration_init(adc_unit_t unit, adc_channel_t channel, adc_atten_t atten, adc_cali_handle_t *out_handle)
{
    adc_cali_handle_t handle = NULL;
    esp_err_t ret = ESP_FAIL;
    bool calibrated = false;

#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
    if (!calibrated) {
        ESP_LOGI(TAG, "calibration scheme version is %s", "Curve Fitting");
        adc_cali_curve_fitting_config_t cali_config = {
            .unit_id = unit,
            .chan = channel,
            .atten = atten,
            .bitwidth = ADC_BITWIDTH_DEFAULT,
        };
        ret = adc_cali_create_scheme_curve_fitting(&cali_config, &handle);
        if (ret == ESP_OK) {
            calibrated = true;
        }
    }
#endif

#if ADC_CALI_SCHEME_LINE_FITTING_SUPPORTED
    if (!calibrated) {
        ESP_LOGI(TAG, "calibration scheme version is %s", "Line Fitting");
        adc_cali_line_fitting_config_t cali_config = {
            .unit_id = unit,
            .atten = atten,
            .bitwidth = ADC_BITWIDTH_DEFAULT,
        };
        ret = adc_cali_create_scheme_line_fitting(&cali_config, &handle);
        if (ret == ESP_OK) {
            calibrated = true;
        }
    }
#endif
    *out_handle = handle;
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Calibration Success");
    } else if (ret == ESP_ERR_NOT_SUPPORTED || !calibrated) {
        ESP_LOGW(TAG, "eFuse not burnt, skip software calibration");
    } else {
        ESP_LOGE(TAG, "Invalid arg or no memory");
    }

    return calibrated;
}

/* Voltage scaling constants: vtarget_mV = adc_mV * NUM / DEN */
#define ADC_VTARGET_SCALE_NUM  (34 * 958)
#define ADC_VTARGET_SCALE_DEN  9470

static void AD_CH1_init(void)
{
    int voltage;
    adc_oneshot_unit_init_cfg_t init_config = {
        .unit_id = ADC_UNIT_1,
    };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&init_config, &gbl_adc_handle));

    adc_oneshot_chan_cfg_t channel_config = {
        //.bitwidth = ADC_BITWIDTH_DEFAULT,
        .atten = ADC_ATTEN_DB_12,  // Allows input voltage up to ~3.3V
        .bitwidth = ADC_BITWIDTH_DEFAULT, //ADC_BITWIDTH_12,
    };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(gbl_adc_handle, ADC_CHANNEL_0, &channel_config));

    bool do_calibration1_chan0 = adc_calibration_init(ADC_UNIT_1, ADC_CHANNEL_0,ADC_ATTEN_DB_12, &adc1_cali_chan0_handle);

    int adc_raw;
    esp_err_t result = adc_oneshot_read(gbl_adc_handle, ADC_CHANNEL_0, &adc_raw);

    if (result == ESP_OK) {
        ESP_LOGI(TAG, "ADC Raw: %d", adc_raw);
        if (do_calibration1_chan0) {
            ESP_ERROR_CHECK(adc_cali_raw_to_voltage(adc1_cali_chan0_handle,adc_raw, &voltage));
            int vtarget = voltage * ADC_VTARGET_SCALE_NUM / ADC_VTARGET_SCALE_DEN;
            ESP_LOGI(TAG, "ADC%d Channel[%d] Cali Voltage: %d mV vtarget=%d mV", ADC_UNIT_1 + 1, ADC_CHANNEL_0, voltage, vtarget);
        }
    } else {
        ESP_LOGE(TAG, "ADC Read Failed");
    }
}

#if ESP32JTAG_BOARD
static void setup_pwm1()
{
    ledc_timer_config_t ledc_timer1 = {
        .speed_mode       = LEDC_LOW_SPEED_MODE,
        .timer_num        = LEDC_TIMER_1,
        //.duty_resolution  = LEDC_TIMER_13_BIT,      // Resolution of PWM duty (2^13 = 8192 levels)
        .duty_resolution  = LEDC_TIMER_10_BIT,      // Resolution of PWM duty (2^10 = 1024 levels)
        .freq_hz          = 5000,                   // Frequency in Hertz
        .clk_cfg          = LEDC_AUTO_CLK
    };
    ledc_timer_config(&ledc_timer1);

    ledc_channel_config_t ledc_channel1 = {
        .speed_mode     = LEDC_LOW_SPEED_MODE,
        .channel        = LEDC_CHANNEL_1,
        .timer_sel      = LEDC_TIMER_1,
        .intr_type      = LEDC_INTR_DISABLE,
        .gpio_num       = 16,                       // GPIO where PWM is output
        .duty           = 0,                        // Initial duty cycle (0%)
        .hpoint         = 0
    };
    ledc_channel_config(&ledc_channel1);

}
#endif

static void set_pwm1_duty(uint16_t duty) {
    // Set brightness
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_1, duty);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_1);
}


// Helper to get font width if not defined as macro
// Assuming standard usage as per GUI_Paint.h
#ifndef PAINT_CHAR_WIDTH
#define PAINT_CHAR_WIDTH 12
#endif

static void print_sta_info(void)
{
    wifi_sta_list_t sta_list;
    esp_err_t ret = esp_wifi_ap_get_sta_list(&sta_list);

    static int prev_num_stations = -1;  /* Retain last LCD state across calls */
    static int prev_rssi = 1;           /* Retain last LCD state across calls */

    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Number of connected stations: %d", sta_list.num);

        int current_rssi = 0;
        if(sta_list.num > 0){
             current_rssi = sta_list.sta[0].rssi;
        }

        // Update LCD if changed
        if (sta_list.num != prev_num_stations || (sta_list.num > 0 && current_rssi != prev_rssi)) {
            // Use the line below "AP Mode" to avoid width issues
            int x_pos = X_OFFSET + 7 * PAINT_CHAR_WIDTH;//7: "AP Mode"
            int y_pos = 10;

            if (sta_list.num > 0) {
                // "-29dBm" (Yellow)
                char rssi_buf[16];
                snprintf(rssi_buf, sizeof(rssi_buf), " %ddBm", current_rssi);
                Paint_DrawString_EN(x_pos, y_pos, rssi_buf, &CURR_FONT, BLACK, YELLOW);
                x_pos += strlen(rssi_buf) * PAINT_CHAR_WIDTH;

                // "1 Connection" (Green)
                char conn_buf[32];
                snprintf(conn_buf, sizeof(conn_buf), " %d Conn", sta_list.num);
                Paint_DrawString_EN(x_pos, y_pos, conn_buf, &CURR_FONT, BLACK, GREEN);
                x_pos += strlen(conn_buf) * PAINT_CHAR_WIDTH;
            } else {
                 // Clear the line if no connections
                 Paint_DrawString_EN(x_pos, y_pos, "               ", &CURR_FONT, BLACK, BLACK);
            }

            // Clear trailing artifacts
            //Paint_DrawString_EN(x_pos, y_pos, "  ", &CURR_FONT, BLACK, BLACK);

            prev_num_stations = sta_list.num;
            prev_rssi = current_rssi;
        }

        for (int i = 0; i < sta_list.num; i++) {
            wifi_sta_info_t sta = sta_list.sta[i];
            ESP_LOGI(TAG, "STA %d: MAC=%02X:%02X:%02X:%02X:%02X:%02X, RSSI=%d dBm",
                     i + 1,
                     sta.mac[0], sta.mac[1], sta.mac[2],
                     sta.mac[3], sta.mac[4], sta.mac[5],
                     sta.rssi);
        }
    } else {
        ESP_LOGE(TAG, "Failed to get STA list: %s", esp_err_to_name(ret));
    }
}
esp_err_t set_cfga(bool use_porta, bool use_portb, bool use_portc, bool use_portd, bool njtag_swdio, bool swd_gpio){
    esp_err_t ret1 = 0;
    uint8_t tx[3]={0x0,0,0};
    uint8_t rx[3]={0};
    tx[0] = global_data_reg_1 | 0x80;

    global_data_reg_0 &= ~(CFGPA | CFGPB | CFGPC | CFGPD);

    if(use_porta) {
        global_data_reg_0 |= CFGPA;
    }
    if(use_portb) {
        global_data_reg_0 |= CFGPB;
    }
    if(use_portc) {
        global_data_reg_0 |= CFGPC;
    }
    if(use_portd) {
        global_data_reg_0 |= CFGPD;
    }

    if(njtag_swdio){
        global_data_reg_0 |= NJTAG_SWDIO;
    }
    else{
        global_data_reg_0 &= ~NJTAG_SWDIO;
    }
    if(swd_gpio){
        global_data_reg_0 |= SWD_GPIO;
    }
    else{
        global_data_reg_0 &= ~SWD_GPIO;
    }

    tx[1] = global_data_reg_0;
    ESP_LOGI(TAG, "set_cfga: global_data_reg_0=0x%02x use_porta/b/c/d=%d %d %d %d, jtag_swdio=%d, swd_gpio=%d", 
            global_data_reg_0, use_porta, use_portb, use_portc, use_portd, njtag_swdio, swd_gpio);

    ret1 = spi_device4_transfer_data(tx, rx, 3);
    ESP_LOGI(TAG, "rx=0x%02x %02x %02x",rx[0],rx[1], rx[2]);
    return ret1;
}
esp_err_t set_la_input_sel(bool use_test_signal)
{
    esp_err_t ret1 = 0;
    uint8_t tx[3]={0x0,0,0};
    uint8_t rx[3]={0};
    tx[0] = global_data_reg_1 | 0x80;
    if(use_test_signal) {
        global_data_reg_0 |= LA_INPUT_SEL;//use internal cnt1 as LA input
    }
    else{
        global_data_reg_0 &= ~LA_INPUT_SEL;
    }
    tx[1] = global_data_reg_0;

    ret1 = spi_device4_transfer_data(tx, rx, 3);
    ESP_LOGI(TAG, "rx=0x%02x %02x %02x",rx[0],rx[1], rx[2]);
    return ret1;
}
/* Assert (true) or deassert (false) the SRESET signal on Port B pin3.
 * Port B must be configured as PB_UART_SRESET_VTARGET for the signal
 * to physically appear on the connector. */
esp_err_t set_sreset(bool assert_reset)
{
    esp_err_t ret1 = 0;
    uint8_t tx[3]={0x0,0,0};
    uint8_t rx[3]={0};
    tx[0] = global_data_reg_1 | 0x80;
    if (assert_reset) {
        global_data_reg_0 |= SRESET;
    } else {
        global_data_reg_0 &= ~SRESET;
    }
    tx[1] = global_data_reg_0;
    ret1 = spi_device4_transfer_data(tx, rx, 3);
    ESP_LOGI(TAG, "set_sreset(%d): global_data_reg_0=0x%02x", assert_reset, global_data_reg_0);
    return ret1;
}

/* Control Port D (P3) signal output via data_reg_1 outsig_sel field.
 * Requires cfgpd=0 (Port D in Logic Analyzer / non-XVC mode).
 *
 * mode:
 *   0 = PORTD_OUT_TRISTATE   — Port D pins tristate (LA input, default)
 *   1 = PORTD_OUT_COUNTER_LO — drive cnt1[3:0]  (132 MHz free-running counter low nibble)
 *   2 = PORTD_OUT_COUNTER_HI — drive cnt1[7:4]  (counter high nibble)
 *   3 = PORTD_OUT_GPIO       — drive data_reg_1[3:0] directly (value = 0..15)
 */
esp_err_t set_portd_output(uint8_t mode, uint8_t value)
{
    esp_err_t ret;
    uint8_t tx[3] = {0, 0, 0};
    uint8_t rx[3] = {0};

    /* outsig_sel → bits [5:4]; gpio_output_data → bits [3:0]; bit [6] untouched */
    global_data_reg_1 &= ~0x3F;                    /* clear bits [5:0] */
    global_data_reg_1 |= (mode & 0x3) << 4;        /* set outsig_sel */
    if (mode == 3) {
        global_data_reg_1 |= (value & 0xF);        /* set gpio_output_data */
    }

    tx[0] = global_data_reg_1 | 0x80;              /* write flag */
    tx[1] = global_data_reg_0;
    ret = spi_device4_transfer_data(tx, rx, 3);
    ESP_LOGI(TAG, "set_portd_output: mode=%d val=%d data_reg_1=0x%02x", mode, value, global_data_reg_1);
    return ret;
}

/* ── Port D frequency toggle ──────────────────────────────────────────────
 * Drives Port D (P3) pins 0-3 as a square wave (0x00 ↔ 0x0F) using
 * esp_timer.  freq_hz=0 stops the timer and tristates Port D.
 * Valid non-zero values: 125, 250, 500, 1000 Hz.
 */
static esp_timer_handle_t s_portd_toggle_timer = NULL;
static volatile uint8_t   s_portd_toggle_state = 0;

static void portd_toggle_cb(void *arg)
{
    s_portd_toggle_state ^= 1;
    set_portd_output(PORTD_OUT_GPIO, s_portd_toggle_state ? 0x0F : 0x00);
}

esp_err_t set_portd_freq(uint32_t freq_hz)
{
    if (s_portd_toggle_timer) {
        esp_timer_stop(s_portd_toggle_timer);
        esp_timer_delete(s_portd_toggle_timer);
        s_portd_toggle_timer = NULL;
    }
    if (freq_hz == 0) {
        return set_portd_output(PORTD_OUT_TRISTATE, 0);
    }
    uint64_t half_us = 1000000ULL / (2ULL * freq_hz);
    esp_timer_create_args_t args = {
        .callback        = portd_toggle_cb,
        .arg             = NULL,
        .dispatch_method = ESP_TIMER_TASK,
        .name            = "portd_tog",
    };
    esp_err_t ret = esp_timer_create(&args, &s_portd_toggle_timer);
    if (ret != ESP_OK) return ret;
    s_portd_toggle_state = 0;
    return esp_timer_start_periodic(s_portd_toggle_timer, half_us);
}

bool gbl_sw2_gpio48_flag = false;
void check_sw1_sw2(void)
{
    static uint8_t L0 = 0, L48 = 0;    /* Previous GPIO levels, persist across calls */

    // Read the level of GPIO0
    int level_0 = gpio_get_level(PIN_PUSHBUTTON_BOOT_SW1);

    // Read the level of GPIO48
    int level_48 = gpio_get_level(PIN_PUSHBUTTON_SW2);

    if(L0 != level_0 || L48 != level_48){
        if(L48 != level_48 && level_48 == 1){
            gbl_sw2_gpio48_flag = !gbl_sw2_gpio48_flag;
        }
        ESP_LOGI(TAG, "GPIO%d Level: %d | GPIO%d Level: %d gbl_sw2_gpio48_flag=%s",
                PIN_PUSHBUTTON_BOOT_SW1, level_0,
                PIN_PUSHBUTTON_SW2, level_48,
                gbl_sw2_gpio48_flag ? "True":"False");
        L0 = level_0;
        L48 = level_48;
    }
}

/* Draw initial WiFi mode/SSID info on LCD before network is up. */
static void draw_wifi_startup_info(void)
{
    if (g_app_params.mode == APP_MODE_AP) {
        char ap_ip_buf[16] = {0};
        get_ap_ip_str(ap_ip_buf, sizeof(ap_ip_buf));
        Paint_DrawString_EN(X_OFFSET, 10, "AP Mode", &CURR_FONT, BLACK, BLUE);
        Paint_DrawString_EN(X_OFFSET, 10 + 24 * 2, "SSID:", &CURR_FONT, BLACK, YELLOW);
        Paint_DrawString_EN(X_OFFSET + 17 * 5, 10 + 24 * 2, g_app_params.wifi_ssid, &CURR_FONT, BLACK, MAGENTA);
        Paint_DrawString_EN(X_OFFSET, 10 + 24 * 3, "PSWD:", &CURR_FONT, BLACK, YELLOW);
        Paint_DrawString_EN(X_OFFSET + 17 * 5, 10 + 24 * 3, g_app_params.wifi_pass, &CURR_FONT, BLACK, MAGENTA);
    } else {
        Paint_DrawString_EN(X_OFFSET, 10, "ST Mode", &CURR_FONT, BLACK, BLUE);
        Paint_DrawString_EN(X_OFFSET, 10 + 24 * 2, "SSID:", &CURR_FONT, BLACK, YELLOW);
        Paint_DrawString_EN(X_OFFSET + 17 * 5, 10 + 24 * 2, g_app_params.wifi_ssid, &CURR_FONT, BLACK, MAGENTA);
    }
    // Port row: "PORT XVC:2542 GDB:4242"
    Paint_DrawString_EN(X_OFFSET, 10+24*4, "PORT ", &CURR_FONT, BLACK, YELLOW);
    Paint_DrawString_EN(X_OFFSET + 5*PAINT_CHAR_WIDTH,  10+24*4, "XVC:", &CURR_FONT, BLACK, BLUE);
    Paint_DrawString_EN(X_OFFSET + 9*PAINT_CHAR_WIDTH,  10+24*4, "2542", &CURR_FONT, BLACK, MAGENTA);
    Paint_DrawString_EN(X_OFFSET + 13*PAINT_CHAR_WIDTH, 10+24*4, "GDB:", &CURR_FONT, BLACK, BLUE);
    Paint_DrawString_EN(X_OFFSET + 17*PAINT_CHAR_WIDTH, 10+24*4, "4242", &CURR_FONT, BLACK, MAGENTA);
}

/* Draw PA/PB/PC/PD port configuration on LCD. */
static void draw_port_cfg_info(void)
{
    uint32_t y = 10 + 24*5;
    Paint_DrawString_EN(X_OFFSET, y, "PA:          PB:", &CURR_FONT, BLACK, YELLOW);
    Paint_DrawString_EN(X_OFFSET + 3*PAINT_CHAR_WIDTH, y, "LA", &CURR_FONT, BLACK, MAGENTA);
    Paint_DrawString_EN(X_OFFSET + 16*PAINT_CHAR_WIDTH, y,
        (gbl_pb_cfg == PB_UART_SRESET_VTARGET) ? "UART" : "LA", &CURR_FONT, BLACK, MAGENTA);

    y += 15;
    Paint_DrawString_EN(X_OFFSET, y, "PC:          PD:", &CURR_FONT, BLACK, YELLOW);
    Paint_DrawString_EN(X_OFFSET + 3*PAINT_CHAR_WIDTH, y,
        (gbl_pc_cfg == PC_BMP_SWD_JTAG) ? "SWD/JTAG" : "LA", &CURR_FONT, BLACK, MAGENTA);
    Paint_DrawString_EN(X_OFFSET + 16*PAINT_CHAR_WIDTH, y,
        (gbl_pd_cfg == PD_FPGA_XVC) ? "XVC" : "LA", &CURR_FONT, BLACK, MAGENTA);
}

/* Start background tasks (GDB, logic analyser, XVC). */
static void start_background_tasks(void)
{
    if (gbl_pc_cfg == PC_BMP_SWD_JTAG) {
        ESP_LOGI(TAG, "Creating gdb_thread task. gbl_pa_cfg=%d, gbl_pc_cfg=%d", gbl_pa_cfg, gbl_pc_cfg);
        ESP_LOGI(TAG, "Free heap: %u", esp_get_free_heap_size());
        ESP_LOGI(TAG, "To do platform_init() for BMP");
        platform_init();
        ESP_LOGI(TAG, "To do xTaskCreate(&gdb_application_thread) for BMP");
        BaseType_t result = xTaskCreate(&gdb_application_thread, "gdb_thread", 4096, NULL, 15, NULL);
        if (result != pdPASS) {
            ESP_LOGE(TAG, "Failed to create gdb_thread task! Out of memory?");
        }
    }

    logic_analyzer_init();

    if (gbl_pd_cfg == PD_FPGA_XVC) {
        ESP_LOGI(TAG, "To do xTaskCreate(xvc_server_task)");
        xTaskCreate(xvc_server_task, "xvc_server_task", 4096, NULL, 5, NULL);
    }
}

void app_main(void) {

    global_data_reg_0 = 0;
    global_data_reg_1 = 0; // set PC and PD as input

    esp_log_level_set(TAG, ESP_LOG_INFO);
    init_idf_components();

    gbl_spi_rxbuf = heap_caps_malloc(2048, MALLOC_CAP_DMA);
    assert(gbl_spi_rxbuf);

    capture_start_semaphore = xSemaphoreCreateBinary();
    capture_done_semaphore = xSemaphoreCreateBinary();

#if ESP32JTAG_BOARD
    // esp32jtag v1.3/v1.4 pwm to Vout mapping (approx):
    // 0-->3.36V, 6-->3.32V, 8-->3.30V, 12-->3.27V, 18-->3.23V, 22-->3.20V, 32-->3.13V, 40-->3.03V,
    // 100-->2.65V, 120-->2.52V, 123-->2.50V, 200-->1.99V, 220-->1.85V, 228-->1.80V, 235-->1.75V,
    // 250-->1.66V, 271-->1.50V, 275-->1.48V, 300-->1.33V, 318-->1.20V, 320-->1.19V,
    // 322-->1.18V, 350-->1.00V, 400-->0.66V
    uint16_t pwm1_duty = INITIAL_VIO_DUTY;  /* default: 3.3V when key absent */
    {
        char *volt_str = NULL;
        if (storage_alloc_and_read(TARGET_VOLTAGE_KEY, &volt_str) == ESP_OK && volt_str) {
            int idx = atoi(volt_str);
            free(volt_str);
            if (idx >= 0 && idx < (int)VIO_DUTY_TABLE_SIZE) {
                pwm1_duty = vio_duty_table[idx];
            }
        }
        /* If key absent (fresh flash or after factory reset): pwm1_duty stays INITIAL_VIO_DUTY = 3.3V */
    }
    ESP_LOGI(TAG, "VIO voltage: pwm1_duty=%u", pwm1_duty);
    setup_pwm1();
    set_pwm1_duty(pwm1_duty);
#endif
    AD_CH1_init();

    load_network_params();
    load_port_configurations();

    //LCD related starts here ****
    lcd_init();
    lcd_clear(); //clear screen
    Paint_NewImage(LCD_WIDTH, LCD_HEIGHT, 0, BLACK);

    ICE_Init();
    load_fpga();

    draw_wifi_startup_info();

    if (g_app_params.mode == APP_MODE_AP) {
    } else {
        ESP_LOGI(TAG, "Waiting for the network connection...");
    }

    httpd_handle_t http_handle;
    if (web_server_start(&http_handle) != ESP_OK) {
        ESP_LOGE(TAG, "Web server couldn't be started!");
        goto _wait;
    }

    struct network_init_config config = {
        .adapter_name = g_app_params.net_adapter_name,
        .ssid = g_app_params.wifi_ssid,
        .pass = g_app_params.wifi_pass,
        .http_handle = &http_handle
    };

    if (network_start(&config) != ESP_OK) {
         // Fallback logic
        ESP_LOGI(TAG, "Network connection can not be establised. Please check your wifi credentials!");

        // Fallback to AP mode
        if (g_app_params.mode == APP_MODE_STA) {
            ESP_LOGW(TAG, "STA connection failed. Switching to AP Mode fallback.");

            // Save AP mode settings to NVS
            storage_handle_t h;
            if (storage_open_session(&h) == ESP_OK) {
                storage_write_session(h, WIFI_MODE_KEY, "AP", 2);
                storage_close_session(h);
            }
        } else {
            ESP_LOGW(TAG, "Network connection failed!");
        }
        ESP_LOGW(TAG, "Restarting in 3 seconds...");
        vTaskDelay(3000 / portTICK_PERIOD_MS);
        esp_restart();
    }

    //Go on for successful connection
    if (!strcmp(config.adapter_name, "wifiprov")) {
        network_get_sta_credentials(g_app_params.wifi_ssid, g_app_params.wifi_pass);
        storage_write(WIFI_SSID_KEY, g_app_params.wifi_ssid, strlen(g_app_params.wifi_ssid));
        storage_write(WIFI_PASS_KEY, g_app_params.wifi_pass, strlen(g_app_params.wifi_pass));

        ESP_LOGI(TAG, "Wifi credentials have been changed. Restarting in 2 seconds...");
        vTaskDelay(2000 / portTICK_PERIOD_MS);
        esp_restart();
    }

    network_get_my_ip(g_app_params.my_ip);

    // **** IP address display for STA mode goes here ****
    if (g_app_params.mode == APP_MODE_STA) {
        Paint_DrawString_EN(X_OFFSET, 10 + 24, "IP:", &CURR_FONT, BLACK, YELLOW);
        Paint_DrawString_EN(X_OFFSET + 17 * 5, 10 + 24, g_app_params.my_ip, &CURR_FONT, BLACK, MAGENTA);
    } else if (g_app_params.mode == APP_MODE_AP) {
        char ap_ip_buf[16] = {0};
        get_ap_ip_str(ap_ip_buf, sizeof(ap_ip_buf));
        Paint_DrawString_EN(X_OFFSET, 10 + 24, "IP:", &CURR_FONT, BLACK, YELLOW);
        Paint_DrawString_EN(X_OFFSET + 17 * 5, 10 + 24, ap_ip_buf, &CURR_FONT, BLACK, MAGENTA);
    }

    esp_err_t ret_spi = ESP_OK;
    if(gbl_spi_h1 == NULL){
        ret_spi = spi_master_init();
        if (ret_spi != ESP_OK) {
            ESP_LOGE(TAG, "SPI init failed: %s", esp_err_to_name(ret_spi));
        }
    }

    // SPI mode for SWD/JTAG is currently disabled — always use GPIO bitbang.
    // SPI mode has known issues and is not ready for use.
    // In the future, this can be re-enabled: the mode will be configurable via
    // the web interface and persisted in NVS under MCU_INTERFACE_KEY ("SPI"/"GPIO").
    // When that time comes, restore the NVS read block below and remove this forced assignment.
    SPI_nGPIO = false;

    //set PA and PC for swd_jtag. They are mutual excluded: Either use PA or PC for swd_jtag.
    bool b_use_porta = false; //gbl_pa_cfg == PA_BMP_SWD_JTAG;
    bool b_use_portb = gbl_pb_cfg != PB_LOGICANALYZER;
    bool b_use_portc = (gbl_pc_cfg == PC_BMP_SWD_JTAG);
    bool b_use_portd = gbl_pd_cfg != PD_LOGICANALYZER;
    ESP_LOGI(TAG, "To set_cfga(), Using %s for SWD/JTAG, b_use_porta=%d, b_use_portc=%d", SPI_nGPIO ? "SPI" : "GPIO", b_use_porta, b_use_portc);
    //esp32jtag_common.h:esp_err_t set_cfga(bool use_portc, bool use_porta, bool njtag_swdio, bool swd_gpio);
    set_cfga(b_use_porta, b_use_portb, b_use_portc, b_use_portd, true, !SPI_nGPIO); // use_portc, not use_porta, swdio, SPI not gpio

    set_la_input_sel(false);
    start_background_tasks();
    draw_port_cfg_info();
    init_gpio_sw1_sw2();

_wait:

    char *disable_dap_val = NULL;
    bool disable_usb_dap = true;  /* default: disabled */
    if (storage_alloc_and_read(DISABLE_USB_DAP_KEY, &disable_dap_val) == ESP_OK && disable_dap_val) {
        disable_usb_dap = (strcmp(disable_dap_val, "1") == 0);
        free(disable_dap_val);
    }

    if (!disable_usb_dap) {
        xTaskCreate(cherry_dap_task, "cherry_dap_task", 4096, NULL, 5, NULL);
    } else {
        ESP_LOGI(TAG, "USB based CMSIS-DAP and COM port is disabled");
    }

    ESP_LOGI(TAG, "[APP] Free memory: %lu bytes", esp_get_free_heap_size());
    while (true) {
        ESP_LOGI(TAG, "Free internal and DMA memory: %d %d",
                heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                heap_caps_get_free_size(MALLOC_CAP_DMA));

        if (g_app_params.mode == APP_MODE_AP) {
            print_sta_info();
        }

        vTaskDelay(3000 / portTICK_PERIOD_MS);
    }

    esp_restart();
}
