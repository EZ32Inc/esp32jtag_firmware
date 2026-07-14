// SPDX-License-Identifier: Apache-2.0
/*
 * Copyright (C) 2020 Gwenhael Goavec-Merou <gwenhael.goavec-merou@trabucayre.com>
 */

#ifndef SRC_JTAG_HPP_
#define SRC_JTAG_HPP_

#include <stdint.h>
#include <stdbool.h>

#include "esp32JtagBitbang.h"

#if 0
#include "board.hpp"
#include "cable.hpp"
#include "jtagInterface.hpp"
#include "part.hpp"
#endif

void jtag_deinit();
int jtag_init();
#if 0
	/*!
	 * Return constant to describe if read is on rising or falling TCK edge
	 */
	JtagInterface::tck_edge_t getReadEdge() { return _jtag->getReadEdge();}
	/*!
	 * configure TCK edge used for read
	 */
	void setReadEdge(JtagInterface::tck_edge_t rd_edge) {
		_jtag->setReadEdge(rd_edge);
	}
	/*!
	 * Return constant to describe if write is on rising or falling TCK edge
	 */
	JtagInterface::tck_edge_t getWriteEdge() { return _jtag->getWriteEdge();}
	/*!
	 * configure TCK edge used for write
	 */
	void setWriteEdge(JtagInterface::tck_edge_t wr_edge) {
		_jtag->setWriteEdge(wr_edge);
	}
#endif
	/*!
	 * \brief scan JTAG chain to obtain IDCODE. Fill
	 *        a vector with all idcode and another
	 *        vector with irlength
	 * \return number of devices found
	 */
	uint32_t detectChain(unsigned int max_dev);

	/*!
	 * \brief return list of devices in the chain
	 * \return list of devices
	 */
	uint32_t* get_devices_list();// {return _devices_list;}

	/*!
	 * \brief return device index in list
	 * \return device index
	 */
	int get_device_index();// {return device_index;}

	/*!
	 * \brief return current selected device idcode
	 * \return device idcode
	 */
	uint32_t get_target_device_id();// {return _devices_list[device_index];}

	/*!
	 * \brief set index for targeted FPGA
	 * \param[in] index: index in the chain
	 * \return -1 if index is out of bound, index otherwise
	 */
	int device_select(unsigned index);
	/*!
	 * \brief inject a device into list at the begin
	 * \param[in] device_id: idcode
	 * \param[in] irlength: device irlength
	 * \return false if fails
	 */
	bool insert_first(uint32_t device_id, uint16_t irlength);
#if 0
	/*!
	 * \brief return a pointer to the transport subclass
	 * \return a pointer instance of JtagInterface
	 */
	JtagInterface *get_ll_class() {return _jtag;}
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
#endif

	int shiftIR(unsigned char *tdi, unsigned char *tdo, int irlen,	enum tapState_t end_state);// = RUN_TEST_IDLE);
	//int shiftIR(unsigned char tdi, int irlen,enum tapState_t end_state);// = RUN_TEST_IDLE);
	int shiftDR(const uint8_t *tdi, unsigned char *tdo, int drlen,	enum tapState_t end_state);// = RUN_TEST_IDLE);
	int read_write(const uint8_t *tdi, unsigned char *tdo, int len, char last);

	void toggleClk(int nb);
	void go_test_logic_reset();
	void set_state(enum tapState_t newState);//, const uint8_t tdi);// tdi = 1);
	int flushTMS(bool flush_buffer);// = false);
	void flush();// {flushTMS(); jtag_flush();}
	void setTMS(unsigned char tms);

	const char *getStateName(enum tapState_t s);

	/* utilities */
	void setVerbose(int8_t verbose);//{_verbose = verbose;}

	/*!
	 * \brief search in fpga_list and misc_dev_list for a device with idcode
	 *        if found insert idcode and irlength in _devices_list and
	 *        _irlength_list
	 * \param[in] idcode: device idcode
	 * \return false if not found, true otherwise
	 */
	bool search_and_insert_device_with_idcode(uint32_t idcode);
#endif  // SRC_JTAG_HPP_
