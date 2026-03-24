
#pragma once

#include "esp32jtag_common.h"
#include "driver/spi_master.h"

/* doc for FOGA RTL:
      assign IO00 =GPIO4;
      assign IO01 =GPIO5;
      assign IO02 =GPIO6;
      assign IO03 =GPIO14;

      assign IO04 =GPIO38;
      assign IO05 =GPIO39;
      assign IO06 =GPIO40;
      assign IO07 =GPIO41;
      assign IO08 =GPIO42;

      assign IO09 =FPGA_TMS;
      assign IO10 =FPGA_TCK;
      assign IO11 =FPGA_TDI;
      assign IO12 =FPGA_TDO;
*/

#define XVC_USE_SPI 1
#define XVC_SPI_HOST SPI2_HOST

#define PORT 2542
#define XVC_TAG "XVC"

#define XVC_BUF_SIZE (4096)

//#define PIN_RESET_N   GPIO_NUM_42
//#define PIN_SPI_OR_GPIO   GPIO_NUM_39

//void init_spi_for_xvc(void);
extern esp_err_t spi_master_init(void) ;// spi_device_handle_t *spi);
extern esp_err_t spi_device3_transfer_data(const uint8_t *tx_data, uint8_t *rx_data, size_t length);
extern void test_spi();

//void init_gpio_xvc(void);

void xvc_server_task(void *arg);
