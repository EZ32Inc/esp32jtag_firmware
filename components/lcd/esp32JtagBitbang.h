// SPDX-License-Identifier: Apache-2.0
/*
 * Copyright (C) 2020-2022 Gwenhael Goavec-Merou <gwenhael.goavec-merou@trabucayre.com>
 * Copyright (C) 2022 Niklas Ekström <mail@niklasekstrom.nu>
 *
 * libgpiod bitbang driver added by Niklas Ekström <mail@niklasekstrom.nu> in 2022
 */

#ifndef ESP32BITBANG_H
#define ESP32BITBANG_H

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>

//#include "board.hpp"
//#include "jtagInterface.hpp"

//	esp32JtagBitbang(const jtag_pins_conf_t *pin_conf,
//		const std::string &dev, uint32_t clkHZ, int8_t verbose);
//	virtual ~esp32JtagBitbang();

enum tapState_t {
    TEST_LOGIC_RESET = 0,
    RUN_TEST_IDLE = 1,
    SELECT_DR_SCAN = 2,
    CAPTURE_DR = 3,
    SHIFT_DR = 4,
    EXIT1_DR = 5,
    PAUSE_DR = 6,
    EXIT2_DR = 7,
    UPDATE_DR = 8,
    SELECT_IR_SCAN = 9,
    CAPTURE_IR = 10,
    SHIFT_IR = 11,
    EXIT1_IR = 12,
    PAUSE_IR = 13,
    EXIT2_IR = 14,
    UPDATE_IR = 15,
    UNKNOWN = 16,
};
enum device_mode {
    device_RAM_MODE=0,
    device_FLASH_MODE=1,
};
int jtag_setClkFreq(uint32_t clkHZ);
int jtag_writeTMS(const uint8_t *tms_buf, uint32_t len, bool flush_buffer, const uint8_t tdi);
int jtag_writeTDI(const uint8_t *tx, uint8_t *rx, uint32_t len, bool end);
int jtag_toggleClk(uint8_t tms, uint8_t tdi, uint32_t clk_len);
void jtag_init_bitbang_gpio(void);
//static int jtag_update_pins(uint8_t tck,  uint8_t tms, uint8_t tdi);
int jtag_read_tdo();

int  jtag_get_buffer_size();
bool jtag_isFull();
int  jtag_flush();
#if 0
bool _verbose;

uint8_t _tck_pin;
uint8_t _tms_pin;
uint8_t _tdo_pin;
uint8_t _tdi_pin;

//gpiod_chip *_chip;

int _curr_tms;
int _curr_tdi;

int _curr_tck;
#endif
#if 0
int jtag_shiftIR(unsigned char *tdi, unsigned char *tdo, int irlen,	enum tapState_t end_state);// = RUN_TEST_IDLE);
//int shiftIR(unsigned char tdi, int irlen, tapState_t end_state = RUN_TEST_IDLE);
int jtag_shiftDR(const uint8_t *tdi, unsigned char *tdo, int drlen, enum tapState_t end_state);// = RUN_TEST_IDLE);
//int read_write(const uint8_t *tdi, unsigned char *tdo, int len, char last);

//void go_test_logic_reset();
void jtag_set_state(enum tapState_t newState, const uint8_t tdi);// = 1);
//int flushTMS(bool flush_buffer = false);
//void flush() {flushTMS(); _jtag->flush();}
void jatg_setTMS(unsigned char tms);
void jtag_go_test_logic_reset();
#endif
uint64_t jtag_getClkFreq();

#endif//ESP32BITBANG_H
