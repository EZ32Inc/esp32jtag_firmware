/*
 * ICE.c - interface routines  ICE FPGA
 * 04-04-22 E. Brombaugh
 */

#include <string.h>
#include "ice.h"
#include "esp_log.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "rom/ets_sys.h"
#include "soc/spi_periph.h"
#include "esp_rom_gpio.h"
#include "hal/gpio_hal.h"
#include "esp_attr.h" //for BIT
#include "freertos/semphr.h"

//defined in main.c for comm between webserver and ice.c data capture
//extern SemaphoreHandle_t capture_start_semaphore;
//extern SemaphoreHandle_t capture_done_semaphore;
extern bool gbl_triggered_flag;
extern bool gbl_all_captured_flag;
extern uint32_t gbl_wr_addr_stop_position;
extern uint32_t gbl_trigger_position;

extern esp_err_t spi_master_init(void) ;

//define this to use SPI for configuration which is fast
//comment this line ot use pure GPIO to configure FPGA
//Both method works!
//
//2025-09-13: To use GPIO to configure ICE40UP5K FPGA. SPI mdoe for this usage need to set spics_io_num=-1 and manually control spics and 
//   is not compatible with other SPI master usage, which will let spi controller to control spics and spics_io_num=PIN_NUM_CS.
//#define USE_SPI_ICE_CFG 1

//If not defined it is for U2. Otherwise for U7
//#define U7_SPI 1

//if it is for Artix xc7a35t FPGA
//#define PA_LITE_ARTIX_XC7A35 1

/**
  * @brief  SPI Interface pins
  */
#define ICE_SPI_HOST		SPI2_HOST
#ifdef U7_SPI
#define ICE_SPI_SCK_PIN		8
#define ICE_SPI_MISO_PIN	18
#define ICE_SPI_MOSI_PIN	14
#define ICE_SPI_CS_PIN		21
#define ICE_CDONE_PIN		17
#define ICE_CRST_PIN		15
#elif defined PA_LITE_ARTIX_XC7A35
#define ICE_SPI_SCK_PIN		38 //set_property -dict {PACKAGE_PIN B17 IOSTANDARD LVCMOS33} [get_ports {spi_sclk}]//IO_L11P_SRCC_16 //JM1P21
#define ICE_SPI_MISO_PIN	39 //set_property -dict {PACKAGE_PIN C17 IOSTANDARD LVCMOS33} [get_ports {spi_miso}]//IO_L12N_MRCC_16 //JM1P27
#define ICE_SPI_MOSI_PIN	14 //set_property -dict {PACKAGE_PIN D17 IOSTANDARD LVCMOS33} [get_ports {spi_mosi}]//IO_L12P_MRCC_16 //JM1P25
#define ICE_SPI_CS_PIN		21 //set_property -dict {PACKAGE_PIN B18 IOSTANDARD LVCMOS33} [get_ports {spi_cs  }]//IO_L11N_SRCC_16 //JM1P23
#define ICE_CDONE_PIN		42
#define ICE_CRST_PIN		46
#else
#define ICE_SPI_SCK_PIN		38
#define ICE_SPI_MISO_PIN	39
#define ICE_SPI_MOSI_PIN	14
#define ICE_SPI_CS_PIN		21
#define ICE_CDONE_PIN		42
#define ICE_CRST_PIN		46
#endif

#define ICE_SPI_CS_LOW()	gpio_set_level(ICE_SPI_CS_PIN,0)
#define ICE_SPI_CS_HIGH()	gpio_set_level(ICE_SPI_CS_PIN,1)
//#define ICE_SPI_CS_LOW()   ((void)0)
//#define ICE_SPI_CS_HIGH()   ((void)0)

#define ICE_CRST_LOW()		gpio_set_level(ICE_CRST_PIN,0)
#define ICE_CRST_HIGH()		gpio_set_level(ICE_CRST_PIN,1)
#define ICE_CDONE_GET()		gpio_get_level(ICE_CDONE_PIN)
#define ICE_SPI_DUMMY_BYTE	0xFF
#define ICE_SPI_MAX_XFER	4096

#ifdef PA_LITE_ARTIX_XC7A35
static const char* TAG = "XC7A35";
#else
static const char* TAG = "ICE40";
#endif
//spi_device_handle_t spi = NULL;
extern spi_device_handle_t gbl_spi_h1;

#define ICE_CAPTURE_ICE_CAPTURE_BUFFER_SIZE  (128 * 1024)   /* 128 KB */

uint8_t* psram_buffer = NULL;

/* resource locking */
//SemaphoreHandle_t ice_mutex;
uint16_t get_wr_state_info(uint8_t *buf)
{
    uint16_t wr_addr = 0;
#if 0 //for old version RTL that may shift data 1 bit
    wr_addr =  ((buf[1]>>1)&0x7f) | ((buf[0]<<7)&0x80);
    wr_addr <<= 8;
    wr_addr |= ((buf[2]>>1)&0x7f) | ((buf[1]<<7)&0x80);
#else
    wr_addr = buf[1];
    wr_addr <<= 8;
    wr_addr |= buf[2];
#endif
    ESP_LOGI(TAG, "wr_addr = 0x%04x", wr_addr);

#if 0 //for old version RTL that may shift data 1 bit
    uint8_t status = ((buf[3]>>1)&0x7f) | ((buf[2]<<7)&0x80);;
#else
    uint8_t status = buf[3];
#endif
    //spi_miso_reg[7:0] <= {width_cfg[2:0],reading, capture_stop, la_triggered, 2'd0};
    uint8_t la_triggered = (status>>2)&1;
    uint8_t capture_stop = (status>>3)&1;
    uint8_t reading  = (status>>4)&1;
    uint8_t width_cfg =  (status>>5)&7;

    ESP_LOGI(TAG, "la_triggered=%d capture_stop=%d reading=%d width_cfg=%d",
            la_triggered, capture_stop, reading, width_cfg);

    return wr_addr;
}


#ifndef USE_SPI_ICE_CFG
void init_gpio_spipins_as_gpio(void)
{
    gpio_config_t io_conf = {
        .mode = GPIO_MODE_OUTPUT,
        .pin_bit_mask = (1ULL << ICE_SPI_MOSI_PIN) | (1ULL << ICE_SPI_SCK_PIN) ,
        .pull_down_en = 0,
        .pull_up_en = 0,
        .intr_type = GPIO_INTR_DISABLE
    };
    gpio_config(&io_conf);

    //gpio_set_direction(TDO, GPIO_MODE_INPUT);
    io_conf.mode = GPIO_MODE_INPUT;
    io_conf.pin_bit_mask = (1ULL << ICE_SPI_MISO_PIN);
    gpio_config(&io_conf);

    gpio_set_level(ICE_SPI_SCK_PIN,1);
    gpio_set_level(ICE_SPI_MOSI_PIN, 0);
}
#endif

void init_gpio_ice(void)
{
    gpio_config_t io_conf = {
        .mode = GPIO_MODE_OUTPUT,
        .pin_bit_mask = (1ULL << ICE_SPI_CS_PIN) | (1ULL << ICE_CRST_PIN) ,
        .pull_down_en = 0,
        .pull_up_en = 0,
        .intr_type = GPIO_INTR_DISABLE
    };
    gpio_config(&io_conf);

    //gpio_set_direction(TDO, GPIO_MODE_INPUT);
    io_conf.mode = GPIO_MODE_INPUT;
    io_conf.pin_bit_mask = (1ULL << ICE_CDONE_PIN);
    gpio_config(&io_conf);

	ICE_SPI_CS_HIGH();
	ICE_CRST_HIGH();
}
/*
 * init the FPGA interface
 */
void ICE_Init(void)
{
#ifdef USE_SPI_ICE_CFG
#if 0
    esp_err_t ret;
    spi_bus_config_t buscfg={
        .miso_io_num=ICE_SPI_MISO_PIN,
        .mosi_io_num=ICE_SPI_MOSI_PIN,
        .sclk_io_num=ICE_SPI_SCK_PIN,
        .quadwp_io_num=-1,
        .quadhd_io_num=-1,
        .max_transfer_sz = ICE_SPI_MAX_XFER,
    };

    spi_device_interface_config_t devcfg={
        //.clock_speed_hz=20*1000*1000,           //Clock out at 20 MHz    //It works at 20M!
        .clock_speed_hz=10*1000*1000,           //Clock out at 10 MHz  //also works at 10M
        .mode=0,                                //SPI mode 0
        .spics_io_num=-1,                       //CS pin not used
        .queue_size=7,                          //We want to be able to queue 7 transactions at a time
    };
	
	/* create the mutex for access to the FPGA port */
	vSemaphoreCreateBinary(ice_mutex);

    // Initialize the SPI bus
    ESP_LOGI(TAG, "Initialize SPI");
	gpio_reset_pin(ICE_SPI_MISO_PIN);
	gpio_reset_pin(ICE_SPI_MOSI_PIN);
	gpio_reset_pin(ICE_SPI_SCK_PIN);
    ret=spi_bus_initialize(ICE_SPI_HOST, &buscfg, SPI_DMA_CH_AUTO);
    ESP_ERROR_CHECK(ret);
	
    // Attach the SPI bus
    ret=spi_bus_add_device(ICE_SPI_HOST, &devcfg, &spi);
    ESP_ERROR_CHECK(ret);
#else
    if(gbl_spi_h1 == NULL){
        esp_err_t ret_spi = spi_master_init();
        if (ret_spi != ESP_OK) {
            ESP_LOGE(TAG, "SPI init failed: %s", esp_err_to_name(ret_spi));
            //return;
        }
        else{
            ESP_LOGI(TAG, "SPI init OK!");
            //test_spi();
        }
    }
#endif

#else
    //use GPIO for FPGA config instead of using spi
    init_gpio_spipins_as_gpio();
#endif

    /* Initialize non-SPI GPIOs */
    ESP_LOGI(TAG, "Initialize GPIO for ICE40up5k fpga configuration");
    init_gpio_ice();
}

#ifndef USE_SPI_ICE_CFG
static void fpga_ice40_spi_send_data(const uint8_t *z, size_t sz)
{
    bool hi;

    /* assert chip-select (active low) */
    //*clear |= cs;
    gpio_set_level(ICE_SPI_CS_PIN,0);

    for (size_t n = sz; n > 0; --n, ++z) {
        /* msb down to lsb */
        for (int b = 7; b >= 0; --b) {

            /* Data is shifted out on the falling edge (CPOL=0) */
            //*clear |= clk;
            gpio_set_level(ICE_SPI_SCK_PIN,0);
            //fpga_ice40_delay(delay);

            hi = !!(BIT(b) & *z);
            if (hi) {
                //*set |= pico;
                gpio_set_level(ICE_SPI_MOSI_PIN, 1);
            } else {
                //*clear |= pico;
                gpio_set_level(ICE_SPI_MOSI_PIN, 0);
            }

            /* Data is sampled on the rising edge (CPHA=0) */
            //*set |= clk;
            gpio_set_level(ICE_SPI_SCK_PIN,1);
            //fpga_ice40_delay(delay);
        }
    }

    gpio_set_level(ICE_SPI_SCK_PIN,0);

    /* de-assert chip-select (active low) */
    //*set |= cs;
    //gpio_set_level(ICE_SPI_CS_PIN,1);
    gpio_set_level(ICE_SPI_CS_PIN,1);
}
#endif

#if 0
/*
 * init the FPGA interface
 */
void ICE_Init_orig(void)
{
    esp_err_t ret;
    spi_bus_config_t buscfg={
        .miso_io_num=ICE_SPI_MISO_PIN,
        .mosi_io_num=ICE_SPI_MOSI_PIN,
        .sclk_io_num=ICE_SPI_SCK_PIN,
        .quadwp_io_num=-1,
        .quadhd_io_num=-1,
        .max_transfer_sz = ICE_SPI_MAX_XFER,
    };
    spi_device_interface_config_t devcfg={
        .clock_speed_hz=10*1000*1000,           //Clock out at 10 MHz
        .mode=0,                                //SPI mode 0
        .spics_io_num=-1,                       //CS pin not used
        .queue_size=7,                          //We want to be able to queue 7 transactions at a time
    };
	
	/* create the mutex for access to the FPGA port */
	vSemaphoreCreateBinary(ice_mutex);
	
    /* Initialize the SPI bus */
    ESP_LOGI(TAG, "Initialize SPI");
	gpio_reset_pin(ICE_SPI_MISO_PIN);
	gpio_reset_pin(ICE_SPI_MOSI_PIN);
	gpio_reset_pin(ICE_SPI_SCK_PIN);
    ret=spi_bus_initialize(ICE_SPI_HOST, &buscfg, SPI_DMA_CH_AUTO);
    ESP_ERROR_CHECK(ret);
	
    /* Attach the SPI bus */
    ret=spi_bus_add_device(ICE_SPI_HOST, &devcfg, &spi);
    ESP_ERROR_CHECK(ret);

    /* Initialize non-SPI GPIOs */
	/* pins 4-7 must be reset prior to use to get out of JTAG mode */
    ESP_LOGI(TAG, "Initialize GPIO");
	gpio_reset_pin(ICE_SPI_CS_PIN);
    gpio_set_direction(ICE_SPI_CS_PIN, GPIO_MODE_OUTPUT);
	ICE_SPI_CS_HIGH();
	gpio_reset_pin(ICE_CRST_PIN);
	gpio_set_direction(ICE_CRST_PIN, GPIO_MODE_OUTPUT);
	ICE_CRST_HIGH();
	gpio_reset_pin(ICE_CDONE_PIN);
	gpio_set_direction(ICE_CDONE_PIN, GPIO_MODE_INPUT);
}
#endif
#if 0
/*
 * Write a block of bytes to the ICE SPI
 */
void ICE_SPI_WriteBlk(uint8_t *Data, uint32_t Count)
{
    esp_err_t ret;
    spi_transaction_t t = {0};
	uint32_t bytes;
	
	while(Count)
	{
		bytes = (Count > ICE_SPI_MAX_XFER) ? ICE_SPI_MAX_XFER : Count;
		
		memset(&t, 0, sizeof(spi_transaction_t));
		t.length=8*bytes;	
		t.tx_buffer=Data;               //The data is the cmd itself
		ret=spi_device_polling_transmit(gbl_spi_h1, &t);  //Transmit!
		if (ret != ESP_OK) {
			ESP_LOGE(TAG, "ICE_SPI_WriteBlk: SPI transmit failed (%s)", esp_err_to_name(ret));
			return;
		}

		Count -= bytes;
		Data += bytes;
	}
}

/*
 * Read a block of bytes from the ICE SPI
 */
void ICE_SPI_ReadBlk(uint8_t *Data, uint32_t Count)
{
    esp_err_t ret;
    spi_transaction_t t = {0};
	uint32_t bytes;

	while(Count)
	{
		bytes = (Count > ICE_SPI_MAX_XFER) ? ICE_SPI_MAX_XFER : Count;

		memset(&t, 0, sizeof(spi_transaction_t));
		t.length=8*bytes;
		t.rxlength = t.length;
		t.rx_buffer = Data;
		ret=spi_device_polling_transmit(gbl_spi_h1, &t);  //Transmit!
		if (ret != ESP_OK) {
			ESP_LOGE(TAG, "ICE_SPI_ReadBlk: SPI transmit failed (%s)", esp_err_to_name(ret));
			return;
		}
		
		Count -= bytes;
		Data += bytes;
	}
}
#else
/*
 * Write a block of bytes to the ICE SPI
 */
void ICE_SPI_WriteBlk(uint8_t *Data, uint32_t Count)
{
    esp_err_t ret;
    spi_transaction_t t;
    uint32_t bytes;

    while (Count) {
        bytes = (Count > ICE_SPI_MAX_XFER) ? ICE_SPI_MAX_XFER : Count;

        memset(&t, 0, sizeof(spi_transaction_t));
        t.length = 8 * bytes;     // length in bits
        t.tx_buffer = Data;       // pointer to data to send

        //ESP_LOGI(TAG,"WR bytes=%d",bytes);
        ret = spi_device_transmit(gbl_spi_h1, &t);  // blocking transmit
        assert(ret == ESP_OK);

        Count -= bytes;
        Data  += bytes;
    }
}

void ICE_SPI_WriteReadBlk(uint8_t *wrData, uint8_t *rdData, uint32_t Count)
{
    esp_err_t ret;
    spi_transaction_t t;
    uint32_t bytes;

    while (Count) {
        bytes = (Count > ICE_SPI_MAX_XFER) ? ICE_SPI_MAX_XFER : Count;

        memset(&t, 0, sizeof(spi_transaction_t));
        t.length = 8 * bytes;     // length in bits
        t.tx_buffer = wrData;       // pointer to data to send
        t.rxlength = 8 * bytes;   // must match for read
        t.rx_buffer = rdData;       // pointer to data to read

        //ESP_LOGI(TAG,"WR bytes=%d",bytes);
        ret = spi_device_transmit(gbl_spi_h1, &t);  // blocking transmit
        assert(ret == ESP_OK);

        Count -= bytes;
        wrData  += bytes;
        rdData  += bytes;
    }
}

/*
 * Read a block of bytes from the ICE SPI
 */
extern uint8_t *gbl_spi_rxbuf;
void ICE_SPI_ReadBlk(uint8_t *Data, uint32_t Count)
{
    esp_err_t ret;
    spi_transaction_t t;
    uint32_t bytes;

    //ESP_LOGI(TAG, "Free internal DMA memory: %d", heap_caps_get_free_size(MALLOC_CAP_DMA));
    while (Count) {
        //bytes = (Count > ICE_SPI_MAX_XFER) ? ICE_SPI_MAX_XFER : Count;
        bytes = (Count > 2048) ? 2048 : Count;

        memset(&t, 0, sizeof(spi_transaction_t));
        t.length   = 8 * bytes;   // length in bits
        t.rxlength = 8 * bytes;   // must match for read
        //t.rx_buffer = Data;
        t.rx_buffer = gbl_spi_rxbuf;

        //ESP_LOGI(TAG,"RD bytes=%d",bytes);
        ret = spi_device_transmit(gbl_spi_h1, &t);  // blocking transmit
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "SPI transmit failed: %s", esp_err_to_name(ret));
        }
        assert(ret == ESP_OK);
        memcpy(Data, gbl_spi_rxbuf, bytes);

        Count -= bytes;
        Data  += bytes;
    }
}
#endif
#if 0
void ICE_SPI_WriteBlk(uint8_t *Data, uint32_t Count)
{
    esp_err_t ret;
    spi_transaction_t t;
    uint32_t bytes;

    while (Count) {
        bytes = (Count > ICE_SPI_MAX_XFER) ? ICE_SPI_MAX_XFER : Count;

        memset(&t, 0, sizeof(spi_transaction_t));
        t.length = 8 * bytes;

        // Check if Data is DMA-capable
        if (esp_ptr_dma_capable(Data)) {
            t.tx_buffer = Data;
            ret = spi_device_transmit(gbl_spi_h1, &t);
        } else {
            // Allocate temporary DMA buffer
            uint8_t *dma_buf = heap_caps_malloc(bytes, MALLOC_CAP_DMA);
            assert(dma_buf != NULL);
            memcpy(dma_buf, Data, bytes);

            t.tx_buffer = dma_buf;
            ret = spi_device_transmit(gbl_spi_h1, &t);

            free(dma_buf);
        }

        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "ICE_SPI_WriteBlk failed: %s", esp_err_to_name(ret));
        }

        Count -= bytes;
        Data  += bytes;
    }
}
void ICE_SPI_ReadBlk(uint8_t *Data, uint32_t Count)
{
    esp_err_t ret;
    spi_transaction_t t;
    uint32_t bytes;

    while (Count) {
        bytes = (Count > ICE_SPI_MAX_XFER) ? ICE_SPI_MAX_XFER : Count;

        memset(&t, 0, sizeof(spi_transaction_t));
        t.length   = 8 * bytes;
        t.rxlength = 8 * bytes;

        if (esp_ptr_dma_capable(Data)) {
            t.rx_buffer = Data;
            ret = spi_device_transmit(gbl_spi_h1, &t);
        } else {
            // Allocate temporary DMA buffer
            uint8_t *dma_buf = heap_caps_malloc(bytes, MALLOC_CAP_DMA);
            assert(dma_buf != NULL);

            t.rx_buffer = dma_buf;
            ret = spi_device_transmit(gbl_spi_h1, &t);

            if (ret == ESP_OK) {
                memcpy(Data, dma_buf, bytes);
            } else {
                ESP_LOGE(TAG, "ICE_SPI_ReadBlk failed: %s", esp_err_to_name(ret));
            }

            free(dma_buf);
        }

        Count -= bytes;
        Data  += bytes;
    }
}
#endif
void spi_transfer_byte(uint8_t tx_data, uint8_t rx_data)
{
    esp_err_t ret;
    spi_transaction_t t = {0};
    t.length=8;
    t.rxlength = t.length;
    t.rx_buffer = &rx_data;
    t.tx_buffer = &tx_data;
    //ret=spi_device_polling_transmit(gbl_spi_h1, &t);  //Transmit!
    ret = spi_device_transmit(gbl_spi_h1, &t);  // blocking transmit
    assert(ret==ESP_OK);            //Should have had no issues.
}
/*
 * Bitbang the SPI_SCK - used for new timing config
 */
#ifdef USE_SPI_ICE_CFG
void ICE_SPI_ClkToggle(uint32_t cycles)
{
	/* configure SCK pin for GPIO output */
    esp_rom_gpio_pad_select_gpio(ICE_SPI_SCK_PIN);
	gpio_set_direction(ICE_SPI_SCK_PIN, GPIO_MODE_OUTPUT);
	
	/* toggle for cycles */
	while(cycles--)
	{
		gpio_set_level(ICE_SPI_SCK_PIN, 1);
		gpio_set_level(ICE_SPI_SCK_PIN, 0);
	}
	
	/* restore SCK pin to SPI control */
    esp_rom_gpio_connect_out_signal(ICE_SPI_SCK_PIN, spi_periph_signal[ICE_SPI_HOST].spiclk_out, false, false);
    esp_rom_gpio_connect_in_signal(ICE_SPI_SCK_PIN, spi_periph_signal[ICE_SPI_HOST].spiclk_in, false);
}
#else
void ICE_GPIO_ClkToggle(uint32_t cycles)
{
	/* toggle for cycles */
	while(cycles--)
	{
		gpio_set_level(ICE_SPI_SCK_PIN, 1);
		gpio_set_level(ICE_SPI_SCK_PIN, 0);
	}
}
#endif

#if 0 //does not work
uint8_t ICE_FPGA_Config1(uint8_t *bitmap, uint32_t size)
{

    ESP_LOGI(TAG, "ICE40UP5K FPGA DONE bit = %d at 0", ICE_CDONE_GET());
    ESP_LOGI(TAG, "ICE40UP5K FPGA: reset low then high.");

    //set SPI_SCK high
    gpio_set_level(ICE_SPI_SCK_PIN, 0); 

	/* drop CS bit to signal slave mode */
	ICE_SPI_CS_LOW();

	/* drop reset bit */
	ICE_CRST_LOW();
	
	/* delay */
	ets_delay_us(1);
    //ESP_LOGI(TAG, "ICE40UP5K FPGA DONE bit = %d at 1", ICE_CDONE_GET());

#if 0
	/* Wait for done bit to go inactive */
	uint32_t timeout = 100;
	while(timeout && (ICE_CDONE_GET()==1))
	{
		timeout--;
        //ESP_LOGI(TAG, "ICE40UP5K FPGA DONE bit = %d ", ICE_CDONE_GET());
	}
	if(!timeout)
	{
        ESP_LOGE(TAG, "ICE40UP5K FPGA Done bit didn't respond to Reset! Configure FPGA Failed!");
		return 1;
	}
    //ESP_LOGI(TAG, "ICE40UP5K FPGA DONE bit = %d,as expected!", ICE_CDONE_GET());
#endif

	/* raise reset */
	ICE_CRST_HIGH();
	
	/* delay >1200us to allow FPGA to clear */
	ets_delay_us(1200);
	
    //set SPI_SCK low
    gpio_set_level(ICE_SPI_SCK_PIN, 0);

	ICE_SPI_CS_LOW();
	
	/* send the bitstream */
#ifdef USE_SPI_ICE_CFG
	ICE_SPI_WriteBlk(bitmap, size);
#else
    fpga_ice40_spi_send_data(bitmap, size);
#endif

    //ESP_LOGI(TAG, "ICE40UP5K FPGA DONE bit = %d at 3", ICE_CDONE_GET());
	/* bitbang clock */
#ifdef USE_SPI_ICE_CFG
	ICE_SPI_ClkToggle(49);
#else
	ICE_GPIO_ClkToggle(49);
#endif

	ICE_SPI_CS_HIGH();
    gpio_set_level(ICE_SPI_SCK_PIN, 1);
#if 0
    /* error if DONE not asserted */
    if(ICE_CDONE_GET()==0)
    {
        ESP_LOGE(TAG, "ICE40UP5K FPGA DONE not asserted! Configure FPGA Failed!");
		return 0;
	}
    ESP_LOGI(TAG, "ICE40UP5K FPGA DONE asserted. Configure FPGA OK!");
#endif
	/* no error handling for now */
	return 0;
}
#endif

//uint8_t ICE_FPGA_Config_formal(uint8_t *bitmap, uint32_t size)
uint8_t ICE_FPGA_Config(const uint8_t *bitmap, uint32_t size)
{

    ESP_LOGI(TAG, "ICE40UP5K FPGA DONE bit = %d at 0", ICE_CDONE_GET());
    ESP_LOGI(TAG, "ICE40UP5K FPGA: reset low then high.");

    //set SPI_SCK high
    gpio_set_level(ICE_SPI_SCK_PIN, 1); 

	/* drop CS bit to signal slave mode */
	ICE_SPI_CS_LOW();

	/* drop reset bit */
	ICE_CRST_LOW();
	
	/* delay */
	ets_delay_us(1);

#if 0	
    //ESP_LOGI(TAG, "ICE40UP5K FPGA DONE bit = %d at 1", ICE_CDONE_GET());
	/* Wait for done bit to go inactive */
	uint32_t timeout = 100;
	while(timeout && (ICE_CDONE_GET()==1))
	{
		timeout--;
        //ESP_LOGI(TAG, "ICE40UP5K FPGA DONE bit = %d ", ICE_CDONE_GET());
	}
	if(!timeout)
	{
        ESP_LOGE(TAG, "ICE40UP5K FPGA Done bit didn't respond to Reset! Configure FPGA Failed!");
		return 1;
	}
    //ESP_LOGI(TAG, "ICE40UP5K FPGA DONE bit = %d,as expected!", ICE_CDONE_GET());
#endif	

	/* raise reset */
	ICE_CRST_HIGH();
	
	/* delay >1200us to allow FPGA to clear */
	ets_delay_us(1200);
	
    //set SPI_SCK low
    gpio_set_level(ICE_SPI_SCK_PIN, 0);

	/* send 8 dummy clocks with CS high */
	ICE_SPI_CS_HIGH();

#ifdef USE_SPI_ICE_CFG
	ICE_SPI_ClkToggle(8);
#else
	ICE_GPIO_ClkToggle(8);
#endif
	ICE_SPI_CS_LOW();
	
	/* send the bitstream */
#ifdef USE_SPI_ICE_CFG
	ICE_SPI_WriteBlk((uint8_t *)bitmap, size);
#else
    fpga_ice40_spi_send_data(bitmap, size);
#endif

    //ESP_LOGI(TAG, "ICE40UP5K FPGA DONE bit = %d at 2", ICE_CDONE_GET());
    /* raise CS */
	ICE_SPI_CS_HIGH();

    //ESP_LOGI(TAG, "ICE40UP5K FPGA DONE bit = %d at 3", ICE_CDONE_GET());
	/* bitbang clock */
#ifdef USE_SPI_ICE_CFG
	ICE_SPI_ClkToggle(100);
#else
	ICE_GPIO_ClkToggle(100);
#endif

    /* error if DONE not asserted */
    if(ICE_CDONE_GET()==0)
    {
        ESP_LOGE(TAG, "ICE40UP5K FPGA DONE not asserted! Configure FPGA Failed!");
		return 2;
	}
    ESP_LOGI(TAG, "ICE40UP5K FPGA DONE asserted. Configure FPGA OK!\n");
	
	/* no error handling for now */
	return 0;
}

#if 0 //def USE_SPI_ICE_CFG
/*
 * configure the FPGA
 */
/* New version is closer to Lattice timing */
uint8_t ICE_FPGA_Config_orig(uint8_t *bitmap, uint32_t size)
{
	uint32_t timeout;

	/* drop reset bit */
	ICE_CRST_LOW();
	
	/* delay */
	ets_delay_us(1);
	
	/* drop CS bit to signal slave mode */
	ICE_SPI_CS_LOW();
	
	/* delay at least 200ns */
	ets_delay_us(1);
	
	/* Wait for done bit to go inactive */
	timeout = 100;
	while(timeout && (ICE_CDONE_GET()==1))
	{
		timeout--;
	}
	if(!timeout)
	{
		/* Done bit didn't respond to Reset */
		return 1;
	}

	/* raise reset */
	ICE_CRST_HIGH();
	
	/* delay >1200us to allow FPGA to clear */
	ets_delay_us(1200);
	
	/* send 8 dummy clocks with CS high */
	ICE_SPI_CS_HIGH();
	ICE_SPI_ClkToggle(8);
	ICE_SPI_CS_LOW();
	
	/* send the bitstream */
	ICE_SPI_WriteBlk(bitmap, size);

    /* raise CS */
	ICE_SPI_CS_HIGH();

	/* bitbang clock */
	ICE_SPI_ClkToggle(160);

    /* error if DONE not asserted */
    if(ICE_CDONE_GET()==0)
    {
		return 2;
	}
	
	/* no error handling for now */
	return 0;
}
#endif

#if 0 //these functions are not used by esp32jtag project
/*
 * Write a long to the FPGA SPI port
 */
void ICE_FPGA_Serial_Write(uint8_t Reg, uint32_t Data)
{
	uint8_t tx[5] = {0};

	/* Drop CS */
	ICE_SPI_CS_LOW();
	
	/* msbit of byte 0 is 0 for write */
	tx[0] = (Reg & 0x7f);

	/* send next four bytes */
	tx[1] = ((Data>>24) & 0xff);
	tx[2] = ((Data>>16) & 0xff);
	tx[3] = ((Data>> 8) & 0xff);
	tx[4] = ((Data>> 0) & 0xff);
	
	/* tx SPI transaction */
    esp_err_t ret;
    spi_transaction_t t = {0};
	
    t.length=5*8;                   //Command is 40 bits
    t.tx_buffer=tx;             	//The data is the cmd itself
    ret=spi_device_polling_transmit(gbl_spi_h1, &t);  //Transmit!
    assert(ret==ESP_OK);            //Should have had no issues.
	
	/* Raise CS */
	ICE_SPI_CS_HIGH();
}

/*
 * Read a long from the FPGA SPI port
 */
void ICE_FPGA_Serial_Read(uint8_t Reg, uint32_t *Data)
{
	uint8_t tx[5] = {0}, rx[5] = {0};
	
	/* Drop CS */
	ICE_SPI_CS_LOW();
	
	/* msbit of byte 0 is 1 for read */
	tx[0] = (Reg | 0x80);

	/* tx/rx SPI transaction */
    esp_err_t ret;
    spi_transaction_t t = {0};
	
    t.length=5*8;                   //Command is 40 bits
    t.tx_buffer=tx;             	//The data is the cmd itself
	t.rx_buffer=rx;					//received data
    ret=spi_device_polling_transmit(gbl_spi_h1, &t);  //Transmit!
    assert(ret==ESP_OK);            //Should have had no issues.
	
	/* assemble result */
	*Data = (rx[1]<<24) | (rx[2]<<16) | (rx[3]<<8) | rx[4];
	
	/* Raise CS */
	ICE_SPI_CS_HIGH();
}

/***********************************************************************/
/* I know that ESP32 SPI ports can do memory cmd/addr/data sequencing  */
/* but I'm handling it manually here to avoid constantly reconfiguring */
/***********************************************************************/
/*
 * Write a block of data to the FPGA attached PSRAM via SPI port
 */
void ICE_PSRAM_Write(uint32_t Addr, uint8_t *Data, uint32_t size)
{
	uint8_t header[4];
	
	/* build the PSRAM Write header */
	header[0] = 0x02;					// slow write command
	header[1] = (Addr >> 16) & 0xff;
	header[2] = (Addr >>  8) & 0xff;
	header[3] = (Addr >>  0) & 0xff;
	
	/* Drop CS */
	ICE_SPI_CS_LOW();
	
	/* send header */
	ICE_SPI_WriteBlk(header, 4);
	
	/* send data */
	ICE_SPI_WriteBlk(Data, size);
	
	/* Raise CS */
	ICE_SPI_CS_HIGH();	
}

/*
 * Read a block of data from the FPGA attached PSRAM via SPI port
 */
void ICE_PSRAM_Read(uint32_t Addr, uint8_t *Data, uint32_t size)
{
	uint8_t header[4];
	
	/* build the PSRAM Read header */
	header[0] = 0x03;					// slow read command
	header[1] = (Addr >> 16) & 0xff;
	header[2] = (Addr >>  8) & 0xff;
	header[3] = (Addr >>  0) & 0xff;
	
	/* Drop CS */
	ICE_SPI_CS_LOW();
	
	/* send header */
	ICE_SPI_WriteBlk(header, 4);
	
	/* get data */
	ICE_SPI_ReadBlk(Data, size);
	
	/* Raise CS */
	ICE_SPI_CS_HIGH();
}
#endif

#if 0 //def PA_LITE_ARTIX_XC7A35
static void xc7a35t_test_fpga()
{

	uint8_t data[4];
    uint32_t Addr= 0x55555555;
    ICE_PSRAM_Read(Addr, data, 4);
}
#endif

void logic_analyzer_init(){

    ESP_LOGI(TAG, "logic_analyzer_init started!");

    // Allocate memory from PSRAM
    if (psram_buffer == NULL) {
        psram_buffer = (uint8_t*)heap_caps_malloc(ICE_CAPTURE_BUFFER_SIZE, MALLOC_CAP_SPIRAM);
    }

    if (psram_buffer == NULL) {
        ESP_LOGE(TAG, "Failed to allocate %u bytes from PSRAM!\n", ICE_CAPTURE_BUFFER_SIZE);
    } else {
        ESP_LOGI(TAG, "Successfully allocated %u bytes at address %p from PSRAM.\n", ICE_CAPTURE_BUFFER_SIZE, psram_buffer);
    }

    ESP_LOGI(TAG, "Free internal DMA memory: %d", heap_caps_get_free_size(MALLOC_CAP_DMA));

    if(gbl_spi_h1 == NULL){
        esp_err_t ret_spi = spi_master_init();
        if (ret_spi != ESP_OK) {
            ESP_LOGE(TAG, "SPI init failed: %s", esp_err_to_name(ret_spi));
            //return;
        }
        else{
            ESP_LOGI(TAG, "SPI init OK!");
            //test_spi();
        }
    }
}

#if 0 //Not need any more
void logic_analyzer_thread(void *arg) {

    ESP_LOGI(TAG, "logic_analyzer_thread started!");

    // Allocate memory from PSRAM
    if (psram_buffer == NULL) {
        psram_buffer = (uint8_t*)heap_caps_malloc(ICE_CAPTURE_BUFFER_SIZE, MALLOC_CAP_SPIRAM);
    }

    if (psram_buffer == NULL) {
        ESP_LOGE(TAG, "Failed to allocate %u bytes from PSRAM!\n", ICE_CAPTURE_BUFFER_SIZE);
    } else {
        ESP_LOGI(TAG, "\nSuccessfully allocated %u bytes at address %p from PSRAM.\n", ICE_CAPTURE_BUFFER_SIZE, psram_buffer);
    }

    ESP_LOGI(TAG, "Free internal DMA memory: %d", heap_caps_get_free_size(MALLOC_CAP_DMA));
#if 1
    if(gbl_spi_h1 == NULL){
        esp_err_t ret_spi = spi_master_init();
        if (ret_spi != ESP_OK) {
            ESP_LOGE(TAG, "\nSPI init failed: %s", esp_err_to_name(ret_spi));
            //return;
        }
        else{
            ESP_LOGI(TAG, "SPI init OK!");
            //test_spi();
        }
    }
#endif
    //spi2fpga_test_rtl();
    //spi2fpga_test();
    //static int cnt = 0;
    while (1) {
        //if (xSemaphoreTake(capture_start_semaphore, portMAX_DELAY) == pdTRUE) {
        if(gbl_capture_started){
            // LEGACY CODE DISABLED:
            // This thread used to handle capture, but now start_capture() is called directly
            // from the web handler. We disable this block to prevent double execution
            // and blocking behavior.
            
            /*
            ESP_LOGI(TAG, "Capture signal received. Starting data capture...");
            gbl_triggered_flag = false;
            gbl_all_captured_flag = false;
            //ESP_LOGI(TAG, "capture_data() to start in ice.c");
            capture_data();

            gbl_capture_started = false;
            gbl_triggered_flag = true;
            gbl_all_captured_flag = true;
            */
            
            // Just clear the flag to avoid infinite loop if it gets set
            gbl_capture_started = false;
            
            //ESP_LOGI(TAG, "capture_data() done in ice.c gbl_triggered_flag=%s gbl_all_captured_flag=%s",
            //        gbl_triggered_flag ? "true" : "false",
            //        gbl_all_captured_flag? "true" : "false");
        }
        //xc7a35t_test_fpga();
        vTaskDelay(pdMS_TO_TICKS(500));  // Give time to the scheduler
        //if(cnt++%10==0)
        //    ESP_LOGI(TAG, "gbl_capture_started =%d", gbl_capture_started);
        
    }//while (1)

    vTaskDelete(NULL);
}
#endif

static uint64_t set_ch_trigger_mode(uint64_t trig_mode_in, uint8_t channel, uint8_t mode) {
    // Verilog's "localparam integer SHIFT_AMT = 3;"
    const int SHIFT_AMT = 3;

    // The starting bit position for the channel's 3-bit mode.
    // Equivalent to Verilog's `channel * SHIFT_AMT`.
    int bit_position = channel * SHIFT_AMT;

    // 1. Create a mask to clear the existing 3 bits for the channel.
    //    Verilog: `~ (3'b111 << (channel * SHIFT_AMT))`
    //    C: `~((uint64_t)0x7 << bit_position)`
    //    0x7 is 0b111 in binary, representing the 3 bits.
    uint64_t mode_mask = ~((uint64_t)0x7 << bit_position);

    // 2. Clear the old value for the channel using a bitwise AND with the mask.
    //    Verilog: `trig_mode = trig_mode & mode_mask;`
    uint64_t new_trig_mode = trig_mode_in & mode_mask;

    // 3. Set the new mode value for the channel.
    //    Verilog: `trig_mode = trig_mode | (mode << (channel * SHIFT_AMT));`
    //    C: `new_trig_mode | ((uint64_t)mode << bit_position)`
    new_trig_mode = new_trig_mode | ((uint64_t)mode << bit_position);

    return new_trig_mode;
}

const uint8_t READING_START_BIT    = 0;
const uint8_t CH_SEL_START_BIT     = 1;
const uint8_t EN_TRIGGER_START_BIT = 5;
const uint8_t EN_CAPTURE_START_BIT = 6;
const uint8_t TRIG_OR_AND_START_BIT= 7;

const uint8_t MODE_MASKED       = 0;//3'b000;
const uint8_t MODE_HIGH         = 1;//3'b001;
const uint8_t MODE_LOW          = 2;//3'b010;
const uint8_t MODE_RISING_EDGE  = 3;//3'b011;
const uint8_t MODE_FALLING_EDGE = 4;//3'b100;
const uint8_t MODE_TRANSITION   = 5;//3'b101;

//for read_capture_status(). Need to save the settings when doing start_capture()
// Then use them when doing read_capture_status()
static uint8_t gbl_trigger_pos = 0;
static uint8_t gbl_width_cfg_clk_div_low2b = 0;
static uint8_t gbl_clk_divider = 0;

/*
 * Start capture - Initialize and start the logic analyzer capture
 * This function configures the FPGA and starts capture but does NOT wait for trigger
 * Returns immediately after starting capture
 * 
 * @param without_trigger: If true, forces immediate capture regardless of trigger settings
 */
void start_capture(bool without_trigger)
{
    ESP_LOGI(TAG, "=== start_capture(without_trigger=%d) BEGIN ===", without_trigger);
    
    uint64_t trig_mode = 0;
    uint8_t width_cfg = 0; // 000 = 16-CH
    uint8_t ch_sel = 0;
    uint8_t trig_or_and = gbl_trigger_mode_or ? 1 : 0;
    
    // Check if trigger is enabled or forced immediate capture
    extern bool gbl_trigger_enabled;
    if (!gbl_trigger_enabled || without_trigger) {
        // If trigger is disabled OR without_trigger is requested, we want to capture immediately.
        // We do this by setting AND mode (0) and masking all channels.
        // AND of all masked channels (which return true) results in true.
        trig_or_and = 0;
        //Note: Actually need it to be OR to always trigger!!!
        //trig_or_and =1;
        ESP_LOGI(TAG, "start_capture: Forcing immediate trigger (AND mode) because %s", 
                 without_trigger ? "without_trigger=true" : "gbl_trigger_enabled=false");
    }

    uint8_t enable_capture = 0;
    uint8_t enable_trigger = 0;
    uint8_t reading = 0;
    uint8_t clk_div_low2b = 0;
    
    uint8_t buf[16];
    uint8_t i = 0;
    
    gbl_triggered_flag = false;
    gbl_all_captured_flag = false;
    // Parameters to set
    uint8_t clk_divider = gbl_sample_rate_reg;//255, 264M;0, 132M; 1, 132/2; 2, 132/3; ...n, 132/(n+1); 254, 132/255M
    //uint32_t trigger_pos_in_samples =  64*1024 - trigger_pos*512 - 11;
    //if gbl_trigger_position is 33, means 33% of whole screen which is 64*1024, calculated from end

    uint32_t trigger_pos = 128 * gbl_trigger_position / 100;
    if (trigger_pos > 127) {
        trigger_pos = 127;
    }
    trigger_pos = 127 - trigger_pos; // calculate from beginning
    
    ESP_LOGI(TAG, "start_capture: gbl_trigger_position=%ld trigger_pos=%ld", 
             gbl_trigger_position, trigger_pos);
    ESP_LOGI(TAG, "start_capture: sample_rate_reg=%d trigger_mode_or=%d", 
             clk_divider, trig_or_and);
    
    // Configure CS pin for GPIO output
    esp_rom_gpio_pad_select_gpio(ICE_SPI_CS_PIN);
    gpio_set_direction(ICE_SPI_CS_PIN, GPIO_MODE_OUTPUT);
    ICE_SPI_CS_HIGH();
    
    // Build initial configuration
    uint8_t spi_mosi_reg = (reading << READING_START_BIT) |
        (ch_sel << CH_SEL_START_BIT) |
        (enable_trigger << EN_TRIGGER_START_BIT) |
        (enable_capture << EN_CAPTURE_START_BIT) |
        (trig_or_and << TRIG_OR_AND_START_BIT);
    buf[i++] = spi_mosi_reg;
    
    buf[i++] = clk_divider; // clk_divide[7:0]
    buf[i++] = trigger_pos; // hi_cnt_max[7:0]

    spi_mosi_reg = (width_cfg << 5) | (clk_div_low2b << 3);
    buf[i++] = spi_mosi_reg;

    //save the settings for read_capture_status()
    gbl_trigger_pos = trigger_pos;
    gbl_clk_divider = clk_divider;
    gbl_width_cfg_clk_div_low2b = spi_mosi_reg;

    // Set trig_mode according to gbl_channel_triggers[]
    trig_mode = 0;
    for (int j = 0; j < 16; j++) {
        if (!gbl_trigger_enabled || without_trigger) {
            // Force all channels to MASKED if trigger is disabled or forced immediate
            // Combined with AND mode, this forces immediate trigger
            trig_mode = set_ch_trigger_mode(trig_mode, j, MODE_MASKED);
        }
        else if (gbl_channel_triggers[j] == TRIGGER_DISABLED)       
            trig_mode = set_ch_trigger_mode(trig_mode, j, MODE_MASKED);
        else if (gbl_channel_triggers[j] == TRIGGER_RISING)    
            trig_mode = set_ch_trigger_mode(trig_mode, j, MODE_RISING_EDGE);
        else if (gbl_channel_triggers[j] == TRIGGER_FALLING)   
            trig_mode = set_ch_trigger_mode(trig_mode, j, MODE_FALLING_EDGE);
        else if (gbl_channel_triggers[j] == TRIGGER_CROSSING)  
            trig_mode = set_ch_trigger_mode(trig_mode, j, MODE_TRANSITION);
        else if (gbl_channel_triggers[j] == TRIGGER_HIGH)      
            trig_mode = set_ch_trigger_mode(trig_mode, j, MODE_HIGH);
        else if (gbl_channel_triggers[j] == TRIGGER_LOW)       
            trig_mode = set_ch_trigger_mode(trig_mode, j, MODE_LOW);
        else 
            trig_mode = set_ch_trigger_mode(trig_mode, j, MODE_MASKED);
    }
    //ESP_LOGI(TAG, "start_capture: trig_mode=0x%llx", trig_mode);
    
    // Add trigger mode bytes
    for (int j = 0; j < 6; ++j) {
        spi_mosi_reg = (trig_mode >> ((5 - j) * 8)) & 0xFF;
        buf[i++] = spi_mosi_reg;
    }
    
    // Send initial configuration
    ICE_SPI_CS_LOW();
    ICE_SPI_WriteBlk(buf, i);
    ICE_SPI_CS_HIGH();
    vTaskDelay(pdMS_TO_TICKS(100));
    
    // Enable capture
    i = 0;
    enable_capture = 1;
    spi_mosi_reg = (reading << READING_START_BIT) |
        (ch_sel << CH_SEL_START_BIT) |
        (enable_trigger << EN_TRIGGER_START_BIT) |
        (enable_capture << EN_CAPTURE_START_BIT) |
        (trig_or_and << TRIG_OR_AND_START_BIT);
    buf[i++] = spi_mosi_reg;
    
    ICE_SPI_CS_LOW();
    ICE_SPI_WriteBlk(buf, i);
    ICE_SPI_CS_HIGH();
    vTaskDelay(pdMS_TO_TICKS(100));
    
    // Enable trigger
    i = 0;
    enable_trigger = 1;
    spi_mosi_reg = (reading << READING_START_BIT) |
        (ch_sel << CH_SEL_START_BIT) |
        (enable_trigger << EN_TRIGGER_START_BIT) |
        (enable_capture << EN_CAPTURE_START_BIT) |
        (trig_or_and << TRIG_OR_AND_START_BIT);
    buf[i++] = spi_mosi_reg;
    
    ICE_SPI_CS_LOW();
    ICE_SPI_WriteBlk(buf, i);
    ICE_SPI_CS_HIGH();
#if 0    
    if(without_trigger){
        //disable capture
        i = 0;
        enable_capture = 0;
        reading = 1;
        spi_mosi_reg = (reading << READING_START_BIT) |
            (ch_sel << CH_SEL_START_BIT) |
            (enable_trigger << EN_TRIGGER_START_BIT) |
            (enable_capture << EN_CAPTURE_START_BIT) |
            (trig_or_and << TRIG_OR_AND_START_BIT);
        buf[i++] = spi_mosi_reg;

        ICE_SPI_CS_LOW();
        ICE_SPI_WriteBlk(buf, i);
        ICE_SPI_CS_HIGH();
    }
#endif
    ESP_LOGI(TAG, "start_capture: Capture and trigger enabled, waiting for trigger...");
    ESP_LOGI(TAG, "=== start_capture() END ===");
}

/*
 * Read capture status - Check if trigger has occurred and capture is complete
 * Performs a single status check (non-blocking)
 * Updates global flags: gbl_triggered_flag, gbl_wr_addr_stop_position
 */
void read_capture_status(void)
{
    uint8_t tmpbuff[4];
    uint8_t buf[4] = {0};
    
    uint8_t reading =0, ch_sel=0, enable_trigger=1, enable_capture= 1;
    uint8_t trig_or_and = gbl_trigger_mode_or ? 1 : 0;
    buf[0] = (reading << READING_START_BIT) |
        (ch_sel << CH_SEL_START_BIT) |
        (enable_trigger << EN_TRIGGER_START_BIT) |
        (enable_capture << EN_CAPTURE_START_BIT) |
        (trig_or_and << TRIG_OR_AND_START_BIT);

    //use settings saved
    buf[1] = gbl_clk_divider;
    buf[2] = gbl_trigger_pos;
    buf[3] = gbl_width_cfg_clk_div_low2b;

    ICE_SPI_CS_LOW();
    ICE_SPI_WriteReadBlk(buf, tmpbuff, 4);
    ICE_SPI_CS_HIGH();
    
    uint8_t status = tmpbuff[3];
    // spi_miso_reg[7:0] <= {width_cfg[2:0], reading, capture_stop, la_triggered, 2'd0};
    uint8_t la_triggered = (status >> 2) & 1;
    uint8_t capture_stop = (status >> 3) & 1;
    
    ESP_LOGI(TAG, "read_capture_status: la_triggered=%d capture_stop=%d status=0x%02x", 
             la_triggered, capture_stop, status);
    
    if (capture_stop) {
        uint16_t wr_addr = get_wr_state_info(tmpbuff);
        gbl_wr_addr_stop_position = wr_addr;
        gbl_triggered_flag = true;
        ESP_LOGI(TAG, "read_capture_status: TRIGGERED! wr_addr=%d (%.3f us)", 
                 wr_addr, wr_addr * 4 * 4 / 1000.0);
    } else {
        gbl_triggered_flag = false;
    }
}

/*
 * Read and return capture - Read captured data from FPGA to PSRAM buffer
 * Should only be called after trigger is confirmed via read_capture_status()
 * Data is stored in psram_buffer (128KB)
 */
void read_and_return_capture(void)
{
    ESP_LOGI(TAG, "=== read_and_return_capture() BEGIN ===");
    
    uint8_t buf[16];
    uint8_t tmpbuff[8];
    uint8_t i = 0;
    uint8_t reading = 1;
    uint8_t ch_sel = 0;
    uint8_t enable_trigger = 0;
    uint8_t enable_capture = 0;
    uint8_t trig_or_and = 1;
    uint32_t rd_address_reg = gbl_wr_addr_stop_position;
    
    ESP_LOGI(TAG, "read_and_return_capture: Reading from wr_addr=%d", rd_address_reg);
    
    // Set reading mode
    uint8_t spi_mosi_reg = (reading << READING_START_BIT) |
        (ch_sel << CH_SEL_START_BIT) |
        (enable_trigger << EN_TRIGGER_START_BIT) |
        (enable_capture << EN_CAPTURE_START_BIT) |
        (trig_or_and << TRIG_OR_AND_START_BIT);
    buf[i++] = spi_mosi_reg;
    
    // Set read address
    buf[i++] = (rd_address_reg >> 16) & 0xff;
    buf[i++] = (rd_address_reg >> 8) & 0xff;
    buf[i++] = (rd_address_reg) & 0xff;
    
    ICE_SPI_CS_LOW();
    ICE_SPI_WriteReadBlk(buf, tmpbuff, i);
    
    // Read captured data, total 128K
    ESP_LOGI(TAG, "read_and_return_capture: Reading 128KB of captured data...");
    for (uint8_t j = 0; j < 64; ++j) {
        ICE_SPI_ReadBlk(psram_buffer + 2048 * j, 2048);
    }
    
    ICE_SPI_CS_HIGH();
    
    ESP_LOGI(TAG, "read_and_return_capture: Data read complete, 128KB stored in psram_buffer");
    ESP_LOGI(TAG, "=== read_and_return_capture() END ===");
}
#if 0
/*
 * DEPRECATED: capture_data() - Legacy function for backward compatibility
 * This function is deprecated. Use the new functions instead:
 *   - start_capture()
 *   - read_capture_status() (called repeatedly until triggered)
 *   - read_and_return_capture()
 * 
 * This wrapper calls all three functions sequentially with a blocking wait loop
 */
void capture_data()
{
    ESP_LOGW(TAG, "*** DEPRECATED: capture_data() called - consider using new API ***");
    ESP_LOGI(TAG, "capture_data: Calling start_capture()...");
    
    // Start the capture
    start_capture(false); // Default to respecting trigger settings
    
    // Wait for trigger (blocking loop similar to original implementation)
    ESP_LOGI(TAG, "capture_data: Polling for trigger...");
    int poll_count = 0;
    while (1) {
        read_capture_status();
        
        if (gbl_triggered_flag) {
            ESP_LOGI(TAG, "capture_data: Trigger detected after %d polls", poll_count);
            break;
        }
        
        poll_count++;
        vTaskDelay(pdMS_TO_TICKS(10));
        
        // Safety timeout (same as original ~100000 iterations)
        if (poll_count > 10000) {
            ESP_LOGW(TAG, "capture_data: Timeout waiting for trigger");
            return;
        }
    }
    
    // Read the captured data
    ESP_LOGI(TAG, "capture_data: Calling read_and_return_capture()...");
    read_and_return_capture();
    
    ESP_LOGI(TAG, "capture_data: Complete");
}
#endif
