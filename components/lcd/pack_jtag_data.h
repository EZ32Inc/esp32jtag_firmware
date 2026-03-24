#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

#include "xvc_server.h"

#define LONG_BLOCK_BITS  (256)
//#define SHORT_BLOCK_BITS 128

int proc_spi_transfer(int n_bits, uint8_t *tms, uint8_t *tdi, uint8_t *tdo);
int spi_transfer_jtag_data(int k, uint8_t *tms, uint8_t *tdi, uint8_t *tdo);
void pack_jtag_data(uint32_t n, uint8_t *tms, uint8_t *tdi, uint8_t *tx);
bool check_continues_length(uint32_t len, uint8_t *tms, uint8_t *tdi, uint32_t *m);
esp_err_t proc_short(int n_bits, uint8_t *tms, uint8_t *tdi, uint8_t *tdo);
esp_err_t proc_long(int n_bits, uint8_t *tms, uint8_t *tdi, uint8_t *tdo);

