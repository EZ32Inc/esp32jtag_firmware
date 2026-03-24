#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "descriptors.h"

#include "../port_cfg.h"

// Functions for web server handlers
const char* get_port_a_description(const char* val) {
    if (!val) return "N/A";
    
    int v = atoi(val);
    switch (v) {
        case PA_LOGICANALYZER: return "Logic Analyzer";

        case PA_BMP_SWD:       return "SWD";
        case PA_BMP_JTAG:      return "JTAG";
        case PA_OPENOCD_SWD:   return "OpenOCD SWD";
        case PA_OPENOCD_JTAG:  return "OpenOCD JTAG";
        default: return val;
    }
}

const char* get_port_b_description(const char* val) {
    if (!val) return "N/A";
    
    int v = atoi(val);
    switch (v) {
        case PB_LOGICANALYZER:       return "Logic Analyzer";
        case PB_UART_SRESET_VTARGET: return "Vtarget + UART + SReset";
        default: return val;
    }
}

const char* get_port_c_description(const char* val) {
    if (!val) return "N/A";

    int v = atoi(val);
    switch (v) {
        case PC_LOGICANALYZER:    return "Logic Analyzer";
        case PC_BMP_SWD_JTAG:     return "SWD/JTAG";
        case PC_FPGA_JTAG_CFG:    return "FPGA JTAG Configuration";
        case PC_FPGA_SPI_CFG:     return "FPGA SPI Configuration";
        default: return val;
    }
}

const char* get_port_d_description(const char* val) {
    if (!val) return "N/A";

    int v = atoi(val);
    switch (v) {
        case PD_LOGICANALYZER:  return "Logic Analyzer";
        case PD_FPGA_XVC:       return "FPGA XVC";
        case PD_FPGA_JTAG_GPIO: return "FPGA JTAG GPIO";
        case PD_FPGA_SPI_GPIO:  return "FPGA SPI GPIO";
        default: return val;
    }
}

const char* get_target_voltage_description(const char* val) {
    if (!val) return "N/A";
    if (strcmp(val, "0") == 0) return "3.3V";
    if (strcmp(val, "1") == 0) return "2.5V";
    if (strcmp(val, "2") == 0) return "1.8V";
    if (strcmp(val, "3") == 0) return "1.5V";
    if (strcmp(val, "4") == 0) return "1.2V";
    return val;
}

const char* get_sw_mcu_description(const char* val) {
    if (!val) return "N/A";
    if (strcmp(val, "0") == 0) return "BMP (Black Magic Probe)";
    if (strcmp(val, "1") == 0) return "OpenOCD";
    return val;
}

const char* get_wifi_mode_description(const char* val) {
    if (!val) return "N/A";
    if (strcmp(val, "AP") == 0) return "Access Point (AP) Mode";
    if (strcmp(val, "SM") == 0) return "Station (ST) Mode";
    return val;
}

/* Shared buffer and helper — uint8_t fits in 3 decimal digits + NUL */
typedef const char *(*port_desc_fn)(const char *);

static const char *port_description_int(uint8_t val, port_desc_fn fn)
{
    static char cfg_str[4];
    snprintf(cfg_str, sizeof(cfg_str), "%d", val);
    return fn(cfg_str);
}

// Functions that take an integer
const char* get_port_a_description_int(uint8_t val) {
    return port_description_int(val, get_port_a_description);
}

const char* get_port_b_description_int(uint8_t val) {
    return port_description_int(val, get_port_b_description);
}

const char* get_port_c_description_int(uint8_t val) {
    return port_description_int(val, get_port_c_description);
}

const char* get_port_d_description_int(uint8_t val) {
    return port_description_int(val, get_port_d_description);
}
