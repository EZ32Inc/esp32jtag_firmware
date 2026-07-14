#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdio.h>

#ifndef X86DBG
#include "esp_log.h"
#include "pack_jtag_data.h"
#else //for debug in X86 PC, instead of runing on ESP32

#define ESP_LOGI(tag, fmt, ...) printf(fmt "\n", ##__VA_ARGS__)
#define ESP_LOGE(tag, fmt, ...) printf(fmt "\n", ##__VA_ARGS__)

#define LONG_BLOCK_BITS  (256)
#define SHORT_BLOCK_BITS 128
#define esp_err_t int
#define ESP_FAIL -1
#define ESP_OK 0

int proc_spi_transfer(int n_bits, uint8_t *tms, uint8_t *tdi, uint8_t *tdo);
int spi_transfer_jtag_data(bool longShort, int k, uint8_t *tms, uint8_t *tdi, uint8_t *tdo);
void pack_jtag_data_32bit(uint32_t n, uint32_t tms, uint32_t tdi, uint8_t *tx);
void pack_jtag_data(uint32_t n, uint8_t *tms, uint8_t *tdi, uint8_t *tx);
bool check_continues_length(uint32_t len, uint8_t *tms, uint8_t *tdi, uint32_t *m);
//esp_err_t proc_short(int n_bits, uint8_t *tms, uint8_t *tdi, uint8_t *tdo);
//esp_err_t proc_long(int n_bits, uint8_t *tms, uint8_t *tdi, uint8_t *tdo);

esp_err_t spi_device3_transfer_data(const uint8_t *tx_data, uint8_t *rx_data, size_t length){
    return 0;
}

#endif //X86DBG

void pack_jtag_data(uint32_t n, uint8_t *tms, uint8_t *tdi, uint8_t *tx)
{
    for (uint32_t k = 0; k < n; ++k) {
        uint8_t tms_byte = tms[k];
        uint8_t tdi_byte = tdi[k];
        uint8_t upper = 0;
        uint8_t lower = 0;

        // Upper 4 bits: tms[7:4] → bits 7,5,3,1; tdi[7:4] → bits 6,4,2,0
        for (int i = 0; i < 4; ++i) {
            uint8_t tms_bit = (tms_byte >> (7 - i)) & 1;
            uint8_t tdi_bit = (tdi_byte >> (7 - i)) & 1;
            upper |= tms_bit << (7 - 2 * i);
            upper |= tdi_bit << (6 - 2 * i);
        }

        // Lower 4 bits: tms[3:0] → bits 7,5,3,1; tdi[3:0] → bits 6,4,2,0
        for (int i = 0; i < 4; ++i) {
            uint8_t tms_bit = (tms_byte >> (3 - i)) & 1;
            uint8_t tdi_bit = (tdi_byte >> (3 - i)) & 1;
            lower |= tms_bit << (7 - 2 * i);
            lower |= tdi_bit << (6 - 2 * i);
        }

        tx[2 * k]     = upper;
        tx[2 * k + 1] = lower;
    }
}

bool check_continues_length(uint32_t len, uint8_t *tms, uint8_t *tdi, uint32_t *m) {
    //if (len == 0 || !(tms[0] == 0x00 || tms[0] == 0xFF)) {
    if (len == 0) {
        *m = 0;
        return false;
    }

    // Check whether the condition is true or false at the beginning
    bool condition = (tms[0] == 0x00 || tms[0] == 0xFF);
    if (len == 1) {
        *m = 1;
        return condition;
    }

    uint32_t count = 1;

    for (uint32_t i = 1; i < len; ++i) {
        //bool curr_cond = (tms[i] == 0x00 || tms[i] == 0xFF) && (tdi[i] == 0x00 || tdi[i] == 0xFF);
        bool curr_cond = (tms[i] == 0x00 || tms[i] == 0xFF);

        if (curr_cond != condition)
            break;

        count++;
    }

    *m = count;
    return condition;
}
//int proc_spi_transfer(int n_bits, uint8_t *tms, uint8_t *tdi, uint8_t *tdo) {
//    if (n_bits <= 0 || tms == NULL || tdi == NULL || tdo == NULL) {
//        return -1;  // Invalid input
//    }
//
//    uint32_t len = (n_bits + 7) / 8;  // number of bytes needed
//    uint32_t m = 0;
//
//    bool longShort = check_continues_length(len, tms, tdi, &m);
//    if(!m) {
//        ESP_LOGE(XVC_TAG, "Error: %s  length m = 0\n",__func__);//not possible
//        return -2;  // check failed
//    }
//
//    int k = m*8;
//    if (k >n_bits){
//        k = n_bits ; //last bits
//        longShort = false; //short
//    }
//
//    // Now perform the transfer of m bytes
//    int ret = spi_transfer_jtag_data(longShort, k, tms, tdi, tdo);
//
//    return ret;  // Success
//}
int proc_spi_transfer(int n_bits, uint8_t *tms, uint8_t *tdi, uint8_t *tdo) {
    if (n_bits <= 0 || tms == NULL || tdi == NULL || tdo == NULL) {
        return -1;  // Invalid input
    }

    int remaining_bits = n_bits;
    int offset_bits = 0;

    while (remaining_bits > 0) {
        //uint32_t byte_len = (remaining_bits + 7) / 8;
        int k = LONG_BLOCK_BITS; //max TCKs to generate or bits to consume
        if (k > remaining_bits) {
            k = remaining_bits; // last chunk
        }

        int ret = spi_transfer_jtag_data(k,
                                         tms + offset_bits / 8,
                                         tdi + offset_bits / 8,
                                         tdo + offset_bits / 8);
        if (ret < 0) {
            return ret; // Transfer failed
        }

        offset_bits += k;
        remaining_bits -= k;
    }

    return 0; // All transfers successful
}

static void get_tdo(uint8_t *rx, uint8_t *tdo, size_t len_bits) {
    size_t bit_count = 0;
    size_t tdo_byte_index = 0;

    // First extract bits in chunks of 2 rx bytes → 1 tdo byte
    while (bit_count < len_bits + 1) {
        if ((bit_count / 8) >= len_bits / 8 + 1) break;

        uint8_t b0 = rx[tdo_byte_index * 2];
        uint8_t b1 = rx[tdo_byte_index * 2 + 1];

        // Extract bits 7,5,3,1 from each rx byte
        uint8_t part1 = ((b0 >> 7) & 0x01) << 7 |
                        ((b0 >> 5) & 0x01) << 6 |
                        ((b0 >> 3) & 0x01) << 5 |
                        ((b0 >> 1) & 0x01) << 4;

        uint8_t part2 = ((b1 >> 7) & 0x01) << 3 |
                        ((b1 >> 5) & 0x01) << 2 |
                        ((b1 >> 3) & 0x01) << 1 |
                        ((b1 >> 1) & 0x01) << 0;

        tdo[tdo_byte_index] = part1 | part2;

        tdo_byte_index++;
        bit_count += 8;
    }

    // Now shift the entire tdo array to thet by 1 bit
    size_t len_bytes = (len_bits + 1 + 7) / 8;
    for (size_t i = 0; i < len_bytes; i++) {
        uint8_t current = tdo[i]<<1;
        tdo[i] = current | ((tdo[i+1]>>7)&1);
    }
}

int spi_transfer_jtag_data(int n_bits, uint8_t *tms, uint8_t *tdi, uint8_t *tdo) {
    if (n_bits > LONG_BLOCK_BITS || n_bits <= 0 || !tms || !tdi || !tdo) {
        ESP_LOGE(XVC_TAG, "Invalid input to spi_transfer_jtag_data()");
        return -1;  // Invalid input
    }

    //ESP_LOGD(XVC_TAG, "[spi_transfer_jtag_data] Processing %d bits", n_bits);

    //uint8_t *ptdo = tdo;
    uint8_t tx[((LONG_BLOCK_BITS*2+7)/8)+4]={0}; //1 header + 32*2 data = 67 bytes
    uint8_t rx[((LONG_BLOCK_BITS*2+7)/8)+4]={0};

    //n_bits<=4, len = 1; n_bits=6,7,8, len = 2; len= 9,10,11,12;len =3. etc...
    //uint32_t len = (n_bits*2 + 7) / 8;//for each TCK need 1 bit TMS and 1 bit TDI
    
    //1,2,3 tcks: len=2; For 4,5,6,7 TCKs, len=3; for 8,9,10,11 TCKs, len=4; etc..., up to 256 TCKs and len = 66
    int len = (n_bits/4)+1;//for each TCK need 1 bit TMS and 1 bit TDI
    len++; //one byte header

    tx[0] = n_bits-1;//0 means 1 TCK, 1 means 2 TCKs and so on , up to 256-1

    int n_byte = (n_bits+7)/8;
    pack_jtag_data(n_byte, tms, tdi, tx+1);

    esp_err_t ret = spi_device3_transfer_data(tx, rx, len);
    get_tdo(rx+1, tdo, n_bits);
#if 0
    //debug
    //if(len<=8)  
    ESP_LOGI(XVC_TAG, "[spi_transfer_jtag_data] rx[1]=0x%02x tdo[0]=0x%02x,n_bits=%d bytes=%d",rx[1], tdo[0],n_bits, len);
    for(int i =1;i<=(len-1) ;++i) {printf("%02x ",rx[i]);} printf("---RX len =%d\n",len);
    int len_tdo = (n_bits + 7) / 8;
    for(int i =0;i<len_tdo ;++i) {printf("%02x ",tdo[i]);} printf("---TDO len_tdo=%d\n",len_tdo);
#endif
    return ret;
}

#ifdef TEST_UTILITY
void print_tms(int len, uint8_t *tx,  uint32_t m, bool result)
{
    printf("tx:");
    for (int i = 0;i<len;++i)
        printf(" %02x", tx[i]);
    printf("\n");
    printf("test01 Result: %s, m = %u\n\n", result ? "true" : "false", m);
}
int test01()
{
    uint8_t tms[] = {0x00, 0xFF, 0x00, 0x12, 0x34};
    uint8_t tdi[] = {0xFF, 0x00, 0xFF, 0x56, 0x78};
    uint32_t m = 0;

    bool result = check_continues_length(5, tms, tdi, &m);
    print_tms(5,tms, m, result);

    tms[0] = 1;
    result = check_continues_length(5, tms, tdi, &m);
    print_tms(5,tms, m, result);

    tms[0] = 0xff; tms[1] = 0xf0;
    result = check_continues_length(5, tms, tdi, &m);
    print_tms(5,tms, m, result);

    tms[0] = 0xf0; tms[1] = 0x0f;
    result = check_continues_length(5, tms, tdi, &m);
    print_tms(5,tms, m, result);

    return 0;
}
void print_byte_binary(uint8_t byte) {
    printf("0b");
    for (int i = 7; i >= 0; --i) {
        printf("%c", (byte & (1 << i)) ? '1' : '0');
    }
    printf("\n");
}

/*
Explanation:
Each byte in tx holds 4 bits of data, with:

TMS bits at positions: 7, 5, 3, 1

TDI bits at positions: 6, 4, 2, 0

The loop goes from 0 to n-1 bits.

byte_index decides which tx[] byte we're packing.

Within each byte, each tms/tdi pair is placed in descending positions (7-6, 5-4, etc.).

Example:
For n = 5, it packs into tx[0] and tx[1]:

tx[0]: bits 7-0 → TMS0 TDI0 TMS1 TDI1 TMS2 TDI2 TMS3 TDI3

tx[1]: bits 7-6 → TMS4 TDI4 (other bits untouched)

Let me know if you also need the reverse function to unpack this data.
*/
int test02() {
    uint8_t tx[2] = {0};  // 5 bits require at most 2 bytes (4 pairs per byte)
    uint32_t n = 8;
    uint32_t tms = 0b10010101; // TMS bits: bit 0 = 1, bit 1 = 0, bit 2 = 1, bit 3 = 0, bit 4 = 1
    uint32_t tdi = 0b11101100; // TDI bits: bit 0 = 0, bit 1 = 0, bit 2 = 1, bit 3 = 1, bit 4 = 0

    pack_jtag_data(n, tms, tdi, tx);

    printf("test01 Result: \n");
    printf("Packed TX data:\n");
    printf("tms="); print_byte_binary(tms);
    printf("tdi="); print_byte_binary(tdi);
    for (int i = 0; i < 2; ++i) {
        printf("tx[%d] = 0x%02x ", i, tx[i]);
        print_byte_binary(tx[i]);
    }

    return 0;
}
int main()
{
    test01();
    test02();
    return 0;
}
#endif
