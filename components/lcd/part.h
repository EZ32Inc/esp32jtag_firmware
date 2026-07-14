#ifndef PART_H
#define PART_H

#include <stdint.h>

#define IDCODE2MANUFACTURERID(_idcode) ((_idcode >>  1) & 0x7ff)
#define IDCODE2PART(_idcode)           ((_idcode >> 21) & 0x07f)
#define IDCODE2VERS(_idcode)           ((_idcode >> 28) & 0x00f)
typedef struct {
    const char *vendor;
    const char *family;
    const char *part;
    int pins;
} fpga_model_t;

typedef struct {
    uint32_t id;
    fpga_model_t model;
} fpga_entry_t;

typedef struct {
    uint32_t id;
    const char *family; 
} fpga_family_t;

/* device potentially in JTAG chain but not handled */
typedef struct {
	const char *name;
	int irlength;
} misc_device_t;

typedef struct {
    uint32_t id;
    misc_device_t misc_device;
} misc_device_entry_t;

const fpga_model_t *find_fpga_model(uint32_t id);

#endif // PART_H
