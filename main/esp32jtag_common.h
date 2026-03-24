
#pragma once
#include "esp_err.h"

#ifndef SPI_HOST_USED
#define SPI_HOST_USED SPI3_HOST  // SPI3_HOST for LCD and LA SPI controller
#endif

#define ESP32JTAG_BOARD 1

#define UART_PORT_NUM UART_NUM_1
#define GPIO_UART_TXD (43)
#define GPIO_UART_RXD (44)

//GPIOs connected to FPGA
#define GPIO_04  (4)
#define GPIO_05  (5)
#define GPIO_06  (6)
#define GPIO_14  (14)
#define GPIO_38  (38)
#define GPIO_39  (39)
#define GPIO_40  (40)
#define GPIO_41  (41)
#define GPIO_42  (42)

//GPIOs not connected to FPGA
#define GPIO_02  (2)
#define GPIO_03  (3)
#define GPIO_15  (15)
#define GPIO_45  (45)
#define GPIO_46  (46)
#define GPIO_47  (47)

#ifndef SWDIO_RDnWR_PIN
#define SWDIO_RDnWR_PIN (45)
#endif

#ifndef SWDIO_PIN
#define SWDIO_PIN (41)
#endif

#ifndef PIN_NUM_CS1
#define PIN_NUM_CS1   (GPIO_NUM_13)
#endif

#define PIN_NUM_CS0   (GPIO_NUM_21)
#define PIN_NUM_CS2   (GPIO_NUM_11)
#define PIN_NUM_CS3   (GPIO_NUM_5)
#define PIN_NUM_CLK   (GPIO_NUM_38)
#define PIN_NUM_MOSI  (GPIO_NUM_14)
#define PIN_NUM_MISO  (GPIO_NUM_39)

#define PIN_PUSHBUTTON_BOOT_SW1 (0) //(GPIO_NUM_0)
#define PIN_PUSHBUTTON_SW2      (48) //(GPIO_NUM_48)

//from /nvme1t/work/esp32jtag_v1d3_ice4kup/la_src/top.v
// assign {cfgpc, SWD_GPIO, LA_input_sel, cfgpd, cfgpb, cfgpa, njtag_swdio, sreset} = data_reg_0[7:0];

#define SRESET 0x1      //Connect to line 3 of Port B if CFGPB is set to 1, otherwise no connection
#define NJTAG_SWDIO 0x2 //1 to select SWD, 0 to select JTAG
#define CFGPA 0x4       //1 to select SWD/JTAG; 0 to set Port A all to HIZ and as input for logic analyzer
#define CFGPB 0x8       //1 to select to use as UART and sreset; 0 to set Port B all to HIZ and as input for logic analyzer
#define CFGPD 0x10      //1 to select to use FPGA JTAG; 0 to set Port D all to HIZ and as input for logic analyzer
#define LA_INPUT_SEL 0x20 //bit-5 of global_data_reg_0. Select LA input: 0, IO00 to IO15; 1, internal cnt1
#define SWD_GPIO     0x40 //bit-6 of global_data_reg_0. Select if use GPIO as SWD/JTAG signals or SPI2JTAG generated: 0, generated; 1, GPIO
#define CFGPC 0x80      //1 to select SWD/JTAG; 0 to set Port C all to HIZ and as input for logic analyzer

esp_err_t set_la_input_sel(bool use_test_signal);//set or clear LA_INPUT_SEL
esp_err_t set_sreset(bool assert_reset);          //assert/deassert SRESET on Port B pin3

/* Port D output mode constants (data_reg_1 outsig_sel field) */
#define PORTD_OUT_TRISTATE    0  /* tristate — LA input (default) */
#define PORTD_OUT_COUNTER_LO  1  /* drive internal 132 MHz counter bits [3:0] */
#define PORTD_OUT_COUNTER_HI  2  /* drive internal 132 MHz counter bits [7:4] */
#define PORTD_OUT_GPIO        3  /* drive 4-bit value from data_reg_1[3:0] */
esp_err_t set_portd_output(uint8_t mode, uint8_t value); /* mode: PORTD_OUT_* */

extern uint8_t global_data_reg_0; //defined in main/main.c
extern uint8_t global_data_reg_1;//bit 7 to set wr_and_rd or rread only, bit 6 to 0 reserved
esp_err_t set_cfga(bool use_porta, bool use_portb, bool use_portc, bool use_portd, bool njtag_swdio, bool swd_gpio);

esp_err_t spi_master_init(void);
esp_err_t spi_device2_transfer_data(const uint8_t *tx_data, uint8_t *rx_data, size_t length);
esp_err_t spi_device3_transfer_data(const uint8_t *tx_data, uint8_t *rx_data, size_t length);
esp_err_t spi_device4_transfer_data(const uint8_t *tx_data, uint8_t *rx_data, size_t length);

// Logic Analyzer Settings
typedef enum {
    TRIGGER_DISABLED = 0,
    TRIGGER_RISING,
    TRIGGER_FALLING,
    TRIGGER_CROSSING,
    TRIGGER_HIGH,
    TRIGGER_LOW
} trigger_edge_t;

extern bool SPI_nGPIO; //false, GPIO ; true, SPI ; Global variable defiend in openocd-on-esp32jtag/components/blackmagic_esp32/src/platforms/esp32/main/spi2swd.c
extern bool gbl_capture_started;
extern bool gbl_triggered_flag;
extern bool gbl_all_captured_flag;
extern uint32_t gbl_wr_addr_stop_position;
extern uint32_t gbl_trigger_position;

extern uint32_t gbl_sample_rate;
extern bool gbl_trigger_enabled;
extern bool gbl_trigger_mode_or;
extern trigger_edge_t gbl_channel_triggers[16];
extern uint8_t gbl_sample_rate_reg;

extern uint8_t gbl_pa_cfg;
extern uint8_t gbl_pb_cfg;
extern uint8_t gbl_pc_cfg;
extern uint8_t gbl_pd_cfg;
