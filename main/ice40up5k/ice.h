/*
 * ICE.c - interface routines  ICE FPGA
 * 04-04-22 E. Brombaugh
 */
 
#ifndef __ICE__
#define __ICE__

#include "esp_event.h"
#include "freertos/semphr.h"
#include "../esp32jtag_common.h"

#define ICE_CAPTURE_BUFFER_SIZE (128 * 1024)

extern SemaphoreHandle_t ice_mutex;

void ICE_Init(void);
uint8_t ICE_FPGA_Config(const uint8_t *bitmap, uint32_t size);
void ICE_FPGA_Serial_Write(uint8_t Reg, uint32_t Data);
void ICE_FPGA_Serial_Read(uint8_t Reg, uint32_t *Data);
void ICE_PSRAM_Write(uint32_t Addr, uint8_t *Data, uint32_t size);
void ICE_PSRAM_Read(uint32_t Addr, uint8_t *Data, uint32_t size);

// New capture API functions
void start_capture(bool without_trigger);
void read_capture_status(void);
void read_and_return_capture(void);

#endif

