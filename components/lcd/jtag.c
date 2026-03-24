// SPDX-License-Identifier: Apache-2.0
/*
 * Copyright (C) 2020 Gwenhael Goavec-Merou <gwenhael.goavec-merou@trabucayre.com>
 */

#include <unistd.h>
#include <string.h>

#include "part.h"
#include "jtag.h"
#ifdef ENABLE_XVC
#include "xvc_client.h"
#endif

#include "esp_log.h"

#define MAX_DEVICE_LISIT 8
uint32_t _devices_list[MAX_DEVICE_LISIT];
static const char *TAG = "JTAG";
//private
extern int _verbose;
enum tapState_t _state = RUN_TEST_IDLE;
int _tms_buffer_size = 1024;
int _num_tms =0;
unsigned char *_tms_buffer;
//std::string _board_name;
//const std::map<uint32_t, misc_device>& _user_misc_devs;

int device_index=0; /*!< index for targeted FPGA */

// Assume passive devices in JTAG chain are switched to BYPASS mode,
// thus each device requeres 1 bit during SHIFT-DR
unsigned int _dr_bits_before=0, _dr_bits_after=0;
//char *_dr_bits;

// For the above we need for add BYPASS commands for each passive device
unsigned int _ir_bits_before=0, _ir_bits_after=0;
//char * _ir_bits;

char  _irlength_list[MAX_DEVICE_LISIT]; /*!< ordered list of irlength */
extern uint8_t _curr_tdi;

uint32_t* get_devices_list() {return _devices_list;}
int get_device_index() {return device_index;}
uint32_t get_target_device_id() {return _devices_list[device_index];}
void setVerbose(int8_t verbose){_verbose = verbose;}
void flush() {flushTMS(false); jtag_flush();}


#define DEBUG 0

#if DEBUG
#define display(...) \
	do { if (_verbose) fprintf(stdout, __VA_ARGS__);}while(0)
#else
#define display(...) do {}while(0)
#endif

#if 0
/*
 * FT232 JTAG PINS MAPPING:
 * AD0 -> TCK
 * AD1 -> TDI
 * AD2 -> TDO
 * AD3 -> TMS
 */

/* Rmq:
 * pour TMS: l'envoi de n necessite de mettre n-1 comme longueur
 *           mais le bit n+1 est utilise pour l'etat suivant le dernier
 *           front. Donc il faut envoyer 6bits ([5:0]) pertinents pour
 *           utiliser le bit 6 comme etat apres la commande,
 *           le bit 7 corresponds a l'etat de TDI (donc si on fait 7 cycles
 *           l'etat de TDI va donner l'etat de TMS...)
 * transfert/lecture: le dernier bit de IR ou DR doit etre envoye en
 *           meme temps que le TMS qui fait sortir de l'etat donc il faut
 *           pour n bits a transferer :
 *           - envoyer 8bits * (n/8)-1
 *           - envoyer les 7 bits du dernier octet;
 *           - envoyer le dernier avec 0x4B ou 0x6B
 */

Jtag(const cable_t &cable, const jtag_pins_conf_t *pin_conf,
			const string &dev,
			const string &serial, uint32_t clkHZ, int8_t verbose,
			const string &ip_adr, int port,
			const bool invert_read_edge, const string &firmware_path,
			const std::map<uint32_t, misc_device> &user_misc_devs):
			_verbose(verbose > 1),
			_state(RUN_TEST_IDLE),
			_tms_buffer_size(128), _num_tms(0),
			_board_name("nope"), _user_misc_devs(user_misc_devs),
			device_index(0), _dr_bits_before(0), _dr_bits_after(0),
			_ir_bits_before(0), _ir_bits_after(0), _curr_tdi(1)
{
    _jtag = new esp32JtagBitbang(pin_conf, dev, clkHZ, verbose);
}
#endif
int jtag_init()
{
	_tms_buffer = (unsigned char *)malloc(sizeof(unsigned char) * _tms_buffer_size);
	if (_tms_buffer == NULL){
        ESP_LOGE(TAG, "Error: memory allocation failed\n");
        return -1;
    }
	memset(_tms_buffer, 0, _tms_buffer_size);

	detectChain(32);

    //For esp32jtagv1 on board FPGA
    //if(
    return 0;
}

void jtag_deinit()
{
	free(_tms_buffer);
}
uint32_t detectChain(unsigned max_dev)
{
	//char message[256];
	uint8_t rx_buff[4];
	/* WA for CH552/tangNano: write is always mandatory */
	const uint8_t tx_buff[4] = {0xff, 0xff, 0xff, 0xff};
	uint32_t tmp = 0xffffffff;

	/* cleanup */
	//_devices_list.clear();
	//_irlength_list.clear();
	_ir_bits_before = _ir_bits_after = _dr_bits_before = _dr_bits_after = 0;
	go_test_logic_reset();
	set_state(SHIFT_DR);

	if (_verbose)
		ESP_LOGI(TAG,"Raw IDCODE:");

	for (unsigned i = 0; i < max_dev; ++i) {
		read_write(tx_buff, rx_buff, 32, 0);
		tmp = 0;
		for (int ii = 0; ii < 4; ++ii)
			tmp |= (rx_buff[ii] << (8 * ii));

		if (_verbose) {
			//snprintf(message, sizeof(message), "- %d -> 0x%08lx", i, tmp);
			ESP_LOGI(TAG,"- %d -> 0x%08lx", i, tmp);
		}

		if (tmp == 0) {
			//throw std::runtime_error("TDO is stuck at 0");
            ESP_LOGE(TAG,"TDO is stuck at 0");
		}
		if (tmp == 0xffffffff) {
			if (_verbose) {
				//snprintf(message, sizeof(message), "Fetched TDI, end-of-chain");
				ESP_LOGI(TAG,"Fetched TDI, end-of-chain");
			}
			break;
		}

#if 0
		/* search IDCODE in fpga_list and misc_dev_list
		 * since most device have idcode with high nibble masked
		 * we start to search sub IDCODE
		 * if IDCODE has no match: try the same with version unmasked
		 */
		bool found = false;
		/* ckeck highest nibble to prevent confusion between Cologne Chip
		 * GateMate and Efinix Trion T4/T8 devices
		 */
		if (tmp == 0x20000001)
			found = search_and_insert_device_with_idcode(tmp);
		if (!found) /* not specific case -> search for full */
			found = search_and_insert_device_with_idcode(tmp);
		if (!found) /* if full idcode not found -> search for masked */
			found = search_and_insert_device_with_idcode(tmp & 0x0fffffff);
#endif
        const fpga_model_t *found = find_fpga_model(tmp);
		if (found) {
			uint16_t mfg = IDCODE2MANUFACTURERID(tmp);
			uint8_t part = IDCODE2PART(tmp);
			uint8_t vers = IDCODE2VERS(tmp);

            ESP_LOGI(TAG, "ID=0x%08lx, manufacturer: 0x%03x, part: 0x%02x, vers: 0x%x",tmp, mfg, part, vers);
            ESP_LOGI(TAG, "vendor=%s family=%s part=%s", found->vendor, found->family, found->part);
		}
        else{
           ESP_LOGE(TAG,"Unknown device with IDCODE 0x%lx", tmp);
           return tmp;
        }

        //ESP_LOGI(TAG,"aliNote: to simplify it, only check one device");
        //break;
	}
	set_state(TEST_LOGIC_RESET);
	flushTMS(true);
	return tmp;//_devices_list.size();
}
#if 0
bool search_and_insert_device_with_idcode(uint32_t idcode)
{
	int irlength = -1;
	auto dev = fpga_list.find(idcode);
	if (dev != fpga_list.end())
		irlength = dev->second.irlength;
	if (irlength == -1) {
		auto misc = misc_dev_list.find(idcode);
		if (misc != misc_dev_list.end())
			irlength = misc->second.irlength;
	}
	if (irlength == -1) {
		auto misc = this->_user_misc_devs.find(idcode);
		if (misc != this->_user_misc_devs.end())
			irlength = misc->second.irlength;
	}
	if (irlength == -1)
		return false;

	return insert_first(idcode, irlength);
}

bool insert_first(uint32_t device_id, uint16_t irlength)
{
	_devices_list.insert(_devices_list.begin(), device_id);
	_irlength_list.insert(_irlength_list.begin(), irlength);

	return true;
}

int device_select(unsigned index)
{
	if (index > _devices_list.size())
		return -1;
	device_index = index;
	/* get number of devices, in the JTAG chain,
	 * before the selected one
	 */
	_dr_bits_before = _devices_list.size() - device_index - 1;
	/* get number of devices in the JTAG chain
	 * after the selected one
	 */
	_dr_bits_after = device_index;
	_dr_bits = vector<uint8_t>((std::max(_dr_bits_after, _dr_bits_before) + 7)/8, 0);

	/* when the device is not alone and not
	 * the first a serie of bypass must be
	 * send to complete send ir sequence
	 */
	_ir_bits_after = 0;
	for (int i = 0; i < device_index; ++i)
		_ir_bits_after += _irlength_list[i];

	/* send serie of bypass instructions
	 * final size depends on number of device
	 * before targeted and irlength of each one
	 */
	_ir_bits_before = 0;
	for (unsigned i = device_index + 1; i < _devices_list.size(); ++i)
		_ir_bits_before += _irlength_list[i];
	_ir_bits = vector<uint8_t>((std::max(_ir_bits_before, _ir_bits_after) + 7) / 8, 0xff); // BYPASS command is all-ones

	return device_index;
}
#endif

void setTMS(unsigned char tms)
{
	display("%s %x %d %d\n", __func__, tms, _num_tms, (_num_tms >> 3));
	if (_num_tms+1 == _tms_buffer_size * 8)
		flushTMS(false);
	if (tms != 0)
		_tms_buffer[_num_tms>>3] |= (0x1) << (_num_tms & 0x7);
	_num_tms++;
}

/* reconstruct byte sent to TMS pins
 * - use up to 6 bits
 * -since next bit after length is use to
 *  fix TMS state after sent we copy last bit
 *  to bit after next
 * -bit 7 is TDI state for each clk cycles
 */

int flushTMS(bool flush_buffer)
{
	int ret = 0;
	if (_num_tms != 0) {
		display("%s: %d %x\n", __func__, _num_tms, _tms_buffer[0]);

		ret = jtag_writeTMS(_tms_buffer, _num_tms, flush_buffer, _curr_tdi);

		/* reset buffer and number of bits */
		memset(_tms_buffer, 0, _tms_buffer_size);
		_num_tms = 0;
	} else if (flush_buffer) {
		jtag_flush();
	}
	return ret;
}

void go_test_logic_reset()
{
	/* independently to current state 5 clk with TMS high is enough */
	for (int i = 0; i < 6; i++)
		setTMS(0x01);
	flushTMS(false);
	_state = TEST_LOGIC_RESET;
}

int read_write(const uint8_t *tdi, unsigned char *tdo, int len, char last)
{
	flushTMS(false);
	jtag_writeTDI(tdi, tdo, len, last);
	if (last == 1)
		_state = (_state == SHIFT_DR) ? EXIT1_DR : EXIT1_IR;
	return 0;
}

void toggleClk(int nb)
{
	unsigned char c = (TEST_LOGIC_RESET == _state) ? 1 : 0;
	flushTMS(false);
	if (jtag_toggleClk(c, 0, nb) >= 0)
		return;
	//throw std::exception();
	return;
}

int shiftDR(const uint8_t *tdi, unsigned char *tdo, int drlen, enum tapState_t end_state)
{
	/* if current state not shift DR
	 * move to this state
	 */
	if (_state != SHIFT_DR) {
		set_state(SHIFT_DR);
		flushTMS(false);  // force transmit tms state

		//if (_dr_bits_before)//TODO: To be checked!!! 
		//	read_write(_dr_bits.data(), NULL, _dr_bits_before, false);
	}

	/* write tdi (and read tdo) to the selected device
	 * end (ie TMS high) is used only when current device
	 * is the last of the chain and a state change must
	 * be done
	 */
	read_write(tdi, tdo, drlen, _dr_bits_after == 0 && end_state != SHIFT_DR);

	/* if it's asked to move in FSM */
	if (end_state != SHIFT_DR) {
		/* if current device is not the last */
		//if (_dr_bits_after) //TODO: To be checked!!! 
		//	read_write(_dr_bits.data(), NULL, _dr_bits_after, true);  // its the last force
								   // tms high with last bit


		/* move to end_state */
		set_state(end_state);
	}
	return 0;
}
#if 0
int shiftIR(unsigned char tdi, int irlen, enum tapState_t end_state)
{
	if (irlen > 8) {
		ESP_LOGE(TAG,"Error: this method this direct char don't support more than 1 byte");
		return -1;
	}
	return shiftIR(&tdi, NULL, irlen, end_state);
}
#endif
int shiftIR(unsigned char *tdi, unsigned char *tdo, int irlen, enum tapState_t end_state)
{
	display("%s: avant shiftIR\n", __func__);

	/* if not in SHIFT IR move to this state */
	if (_state != SHIFT_IR) {
		set_state(SHIFT_IR);
		//if (_ir_bits_before) //TODO: To be checked!!! 
		//	read_write(_ir_bits.data(), NULL, _ir_bits_before, false);
	}

	display("%s: envoi ircode\n", __func__);

	/* write tdi (and read tdo) to the selected device
	 * end (ie TMS high) is used only when current device
	 * is the last of the chain and a state change must
	 * be done
	 */
	read_write(tdi, tdo, irlen, _ir_bits_after == 0 && end_state != SHIFT_IR);

	/* it's asked to move out of SHIFT IR state */
	if (end_state != SHIFT_IR) {
		/* again if devices after fill '1' */
		//if (_ir_bits_after > 0) //TODO: To be checked!!!
		//	read_write(_ir_bits.data(), NULL, _ir_bits_after, true);
		/* move to the requested state */
		set_state(end_state);
	}

	return 0;
}

void set_state(enum tapState_t newState)//, const uint8_t tdi)
{
    uint8_t tdi = 1;
	_curr_tdi = tdi;
	unsigned char tms = 0;
	while (newState != _state) {
		display("_state : %16s(%02d) -> %s(%02d) ",
			getStateName((enum tapState_t)_state),
			_state,
			getStateName((enum tapState_t)newState), newState);
		switch (_state) {
		case TEST_LOGIC_RESET:
			if (newState == TEST_LOGIC_RESET) {
				tms = 1;
			} else {
				tms = 0;
				_state = RUN_TEST_IDLE;
			}
			break;
		case RUN_TEST_IDLE:
			if (newState == RUN_TEST_IDLE) {
				tms = 0;
			} else {
				tms = 1;
				_state = SELECT_DR_SCAN;
			}
			break;
		case SELECT_DR_SCAN:
			switch (newState) {
			case CAPTURE_DR:
			case SHIFT_DR:
			case EXIT1_DR:
			case PAUSE_DR:
			case EXIT2_DR:
			case UPDATE_DR:
				tms = 0;
				_state = CAPTURE_DR;
				break;
			default:
				tms = 1;
				_state = SELECT_IR_SCAN;
			}
			break;
		case SELECT_IR_SCAN:
			switch (newState) {
			case CAPTURE_IR:
			case SHIFT_IR:
			case EXIT1_IR:
			case PAUSE_IR:
			case EXIT2_IR:
			case UPDATE_IR:
				tms = 0;
				_state = CAPTURE_IR;
				break;
			default:
				tms = 1;
				_state = TEST_LOGIC_RESET;
			}
			break;
			/* DR column */
		case CAPTURE_DR:
			if (newState == SHIFT_DR) {
				tms = 0;
				_state = SHIFT_DR;
			} else {
				tms = 1;
				_state = EXIT1_DR;
			}
			break;
		case SHIFT_DR:
			if (newState == SHIFT_DR) {
				tms = 0;
			} else {
				tms = 1;
				_state = EXIT1_DR;
			}
			break;
		case EXIT1_DR:
			switch (newState) {
			case PAUSE_DR:
			case EXIT2_DR:
			case SHIFT_DR:
			case EXIT1_DR:
				tms = 0;
				_state = PAUSE_DR;
				break;
			default:
				tms = 1;
				_state = UPDATE_DR;
			}
			break;
		case PAUSE_DR:
			if (newState == PAUSE_DR) {
				tms = 0;
			} else {
				tms = 1;
				_state = EXIT2_DR;
			}
			break;
		case EXIT2_DR:
			switch (newState) {
			case SHIFT_DR:
			case EXIT1_DR:
			case PAUSE_DR:
				tms = 0;
				_state = SHIFT_DR;
				break;
			default:
				tms = 1;
				_state = UPDATE_DR;
			}
			break;
		case UPDATE_DR:
		case UPDATE_IR:
			if (newState == RUN_TEST_IDLE) {
				tms = 0;
				_state = RUN_TEST_IDLE;
			} else {
				tms = 1;
				_state = SELECT_DR_SCAN;
			}
			break;
			/* IR column */
		case CAPTURE_IR:
			if (newState == SHIFT_IR) {
				tms = 0;
				_state = SHIFT_IR;
			} else {
				tms = 1;
				_state = EXIT1_IR;
			}
			break;
		case SHIFT_IR:
			if (newState == SHIFT_IR) {
				tms = 0;
			} else {
				tms = 1;
				_state = EXIT1_IR;
			}
			break;
		case EXIT1_IR:
			switch (newState) {
			case PAUSE_IR:
			case EXIT2_IR:
			case SHIFT_IR:
			case EXIT1_IR:
				tms = 0;
				_state = PAUSE_IR;
				break;
			default:
				tms = 1;
				_state = UPDATE_IR;
			}
			break;
		case PAUSE_IR:
			if (newState == PAUSE_IR) {
				tms = 0;
			} else {
				tms = 1;
				_state = EXIT2_IR;
			}
			break;
		case EXIT2_IR:
			switch (newState) {
			case SHIFT_IR:
			case EXIT1_IR:
			case PAUSE_IR:
				tms = 0;
				_state = SHIFT_IR;
				break;
			default:
				tms = 1;
				_state = UPDATE_IR;
			}
			break;
		case UNKNOWN:;
			// UNKNOWN should not be valid...
			//throw std::exception();
            ESP_LOGE(TAG,"UNKNOWN");
		}

		setTMS(tms);
		display("%d %d %d %x\n", tms, _num_tms-1, _state,
			_tms_buffer[(_num_tms-1) / 8]);
	}
	/* force write buffer */
	flushTMS(false);
}

const char *getStateName(enum tapState_t s)
{
	switch (s) {
	case TEST_LOGIC_RESET:
		return "TEST_LOGIC_RESET";
	case RUN_TEST_IDLE:
		return "RUN_TEST_IDLE";
	case SELECT_DR_SCAN:
		return "SELECT_DR_SCAN";
	case CAPTURE_DR:
		return "CAPTURE_DR";
	case SHIFT_DR:
		return "SHIFT_DR";
	case EXIT1_DR:
		return "EXIT1_DR";
	case PAUSE_DR:
		return "PAUSE_DR";
	case EXIT2_DR:
		return "EXIT2_DR";
	case UPDATE_DR:
		return "UPDATE_DR";
	case SELECT_IR_SCAN:
		return "SELECT_IR_SCAN";
	case CAPTURE_IR:
		return "CAPTURE_IR";
	case SHIFT_IR:
		return "SHIFT_IR";
	case EXIT1_IR:
		return "EXIT1_IR";
	case PAUSE_IR:
		return "PAUSE_IR";
	case EXIT2_IR:
		return "EXIT2_IR";
	case UPDATE_IR:
		return "UPDATE_IR";
	default:
		return "Unknown";
	}
}
