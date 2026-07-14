#include <string.h> // For memset
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "nvs_flash.h"
#include "lwip/sockets.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"

#include "esp32jtag_common.h"
#include "xvc_server.h"
#include "pack_jtag_data.h"

// Define the expected 8-byte data structure from the XVC protocol
// The __attribute__((packed)) ensures there are no alignment gaps.
typedef struct __attribute__((packed)) {
    char signature[4]; // Should be "ift:"
    uint32_t length;   // The 4-byte length value
} xvc_shift_header_t;

#if 0 //ndef XVC_USE_SPI //use GPIO bitbang
static bool jtag_read(void)
{
  return gpio_get_level(TDO);//digitalRead(tdo_gpio) & 1;
}

static void jtag_write(uint8_t tck, uint8_t tms, uint8_t tdi)
{
  gpio_set_level(TCK, tck);
  gpio_set_level(TMS, tms);
  gpio_set_level(TDI, tdi);
}

//<=32-bit each time
static uint32_t jtag_xfer(uint32_t n, uint32_t tms, uint32_t tdi)
{
  uint32_t tdo = 0;
  for (uint32_t i = 0; i < n; i++) {
    jtag_write(0, tms & 1, tdi & 1);
    tdo |= jtag_read() << i;
    jtag_write(1, tms & 1, tdi & 1);
    tms >>= 1;
    tdi >>= 1;
  }
  return tdo;
}
#endif //#ifdef XVC_USE_SPI

//int spi2fpga_test();
//int spi2fpga_test_rtl();

static void reverse_bits(uint8_t *buf, int len) {
    if (!buf || len <= 0) return;

    for (int i = 0; i < len; i++) {
        uint8_t b = buf[i];
        // Reverse bits using a fast 8-bit reverse method
        b = (b & 0xF0) >> 4 | (b & 0x0F) << 4;
        b = (b & 0xCC) >> 2 | (b & 0x33) << 2;
        b = (b & 0xAA) >> 1 | (b & 0x55) << 1;
        buf[i] = b;
    }
}

static  int recv_all(int sock, uint8_t *buffer, int total_len) {
    int bytes_read = 0;
    while (bytes_read < total_len) {
        int n = recv(sock, buffer + bytes_read, total_len - bytes_read, 0);
        if (n <= 0) {
            // Error or connection closed
            return -1;
        }
        bytes_read += n;
    }
    return bytes_read;
}

#if 0 //old GPIO bitbang version, for refernce purpose 
static void xvc_handle_client(int sock) {
    char cmd[16];
    //uint8_t tms[XVC_BUF_SIZE], tdi[XVC_BUF_SIZE], tdo[XVC_BUF_SIZE];
    //unsigned char buffer[2048], result[1024];
    //unsigned char buffer[XVC_BUF_SIZE], result[XVC_BUF_SIZE];
    uint8_t *buffer = malloc(XVC_BUF_SIZE);
    uint8_t *result = malloc(XVC_BUF_SIZE);
    int n;
    if (!buffer || !result) {
        ESP_LOGE(XVC_TAG, "malloc failed!");
        return;
    }
    //memset(cmd, 0, 16);
    //while (1){ //(n = recv(sock, cmd, sizeof(cmd), 0)) > 0) {
    while ((n = recv(sock, cmd, 2, 0)) > 0) {
        //n = recv(sock, cmd, 2, 0);
        if(n != 2){
            ESP_LOGE(XVC_TAG, "xvc_handle_client0: %d received, expect 2",n);
            break;
        }
        //cmd[2] = 0;
        //ESP_LOGI(XVC_TAG, "%s received",cmd);
        if (strncmp(cmd, "ge", 2) == 0) {
            ESP_LOGI(XVC_TAG, "getinfo received");
            const char *info = "xvcServer_v1.0:2048\n";
            n = recv(sock, cmd, 6, 0);
            if(n != 6){
                ESP_LOGE(XVC_TAG, "getinfo reading failed: %d received, expect 6",n);
                break;
            }
            send(sock, info, strlen(info), 0);
        //} else if (strncmp(cmd, "settck:", 7) == 0) {
        } else if (strncmp(cmd, "se", 2) == 0) {
            //uint32_t recv_buf;
            ESP_LOGI(XVC_TAG, "settck received");
            n = recv(sock, cmd, 9, 0);
            if(n != 9){
                ESP_LOGE(XVC_TAG, "settck readinf failed: %d received, expect 9",n);
                break;
            }
            send(sock, cmd+5, 4, 0);  // Echo back
        //} else if (strncmp(cmd, "shift:", 6) == 0) {
        } else if (strncmp(cmd, "sh", 2) == 0) {
            //ESP_LOGI(XVC_TAG, "shift received");
            n = recv(sock, cmd, 4, 0);//read remaining chars "ift:"
            if(n != 4){
                ESP_LOGE(XVC_TAG, "shift reading length failed: %d received, expect 4",n);
                break;
            }

            int len;
            n = recv(sock, &len, 4, 0);
            if(n != 4){
                ESP_LOGE(XVC_TAG, "shift reading length failed: %d received, expect 4",n);
                break;
            }

            int nr_bytes = (len + 7) / 8;
            //if ((nr_bytes * 2) > sizeof(buffer)) {
            if ((nr_bytes * 2) > XVC_BUF_SIZE) {
                ESP_LOGE(XVC_TAG, "buffer size exceeded, nr_bytes*2 = %d, > %d", nr_bytes*2, XVC_BUF_SIZE);
                break;
            }
            static uint32_t cnt2=0;
            if(cnt2<200){
                cnt2++;
                ESP_LOGI(XVC_TAG, "SH: nr_bytes=%d len=%d", nr_bytes, len);
            }

            n = recv_all(sock, buffer, nr_bytes * 2);

            if ((nr_bytes * 2) != n) {
                ESP_LOGE(XVC_TAG, "reading data failed nr_bytes = %d, n=%d", nr_bytes, n);
                break;
            }
            memset(result, 0, nr_bytes);

            ESP_LOGD(XVC_TAG, "Number of Bits/bytes  : %d / %d", len, nr_bytes);
            jtag_write(0, 1, 1);

            int bytesLeft = nr_bytes;
            int bitsLeft = len;
            int byteIndex = 0;
            uint32_t tdi, tms, tdo;

            while (bytesLeft > 0) {
                tms = 0;
                tdi = 0;
                tdo = 0;

                int chunk = (bytesLeft >= 4) ? 4 : bytesLeft;

                // Safe copy to 32-bit words
                for (int i = 0; i < chunk; i++) {
                    ((uint8_t*)&tms)[i] = buffer[byteIndex + i];
                    ((uint8_t*)&tdi)[i] = buffer[byteIndex + nr_bytes + i];
                }

                int bits = (chunk == 4) ? 32 : bitsLeft;

                tdo = jtag_xfer(bits, tms, tdi);

                // Copy back result
                for (int i = 0; i < chunk; i++) {
                    result[byteIndex + i] = ((uint8_t*)&tdo)[i];
                }
                static uint32_t cnt1=0;
                if(cnt1<200){
                    cnt1++;
                    ESP_LOGI(XVC_TAG, "LEN=%d TMS=0x%08lx TDI=0x%08lx TDO=0x%08lx", bits, tms,tdi,tdo);
                }

                bytesLeft -= chunk;
                bitsLeft -= bits;
                byteIndex += chunk;
/*
                ESP_LOGD(XVC_TAG, "LEN : %d", bits);
                ESP_LOGD(XVC_TAG, "TMS : 0x%08lx", tms);
                ESP_LOGD(XVC_TAG, "TDI : 0x%08lx", tdi);
                ESP_LOGD(XVC_TAG, "TDO : 0x%08lx", tdo); 
*/
            }

            jtag_write(0, 1, 0);

            ssize_t sent_total = 0;
            while (sent_total < nr_bytes) {
                ssize_t sent_now = send(sock, result + sent_total, nr_bytes - sent_total, 0);
                if (sent_now <= 0) {
                    ESP_LOGE(XVC_TAG, "send failed (ret=%d, errno=%d)", (int)sent_now, errno);
                    break;
                }
                sent_total += sent_now;
            }

        } else {
            ESP_LOGW(XVC_TAG, "Unknown command received");
            break;
        }
    } //while(true)

    free(buffer);
    free(result);

    ESP_LOGI(XVC_TAG, "Client disconnected");
    close(sock);
}
#endif

//new SPI high speed version , tck is half of spi_clk
static void xvc_handle_client_v3(int sock) {
    int n;
    char cmd[16];
    //uint8_t *buffer = malloc(XVC_BUF_SIZE);
    //uint8_t *result = malloc(XVC_BUF_SIZE);
    uint8_t *buffer = (uint8_t *)heap_caps_malloc(XVC_BUF_SIZE, MALLOC_CAP_SPIRAM);
    uint8_t *result = (uint8_t *)heap_caps_malloc(XVC_BUF_SIZE, MALLOC_CAP_SPIRAM);

    if (!buffer || !result) {
        ESP_LOGE(XVC_TAG, "malloc failed!");
        return;
    }
    while ((n = recv(sock, cmd, 2, 0)) > 0) {
        if(n != 2){
            ESP_LOGE(XVC_TAG, "xvc_handle_client0: %d received, expect 2",n);
            break;
        }
        //cmd[2] = 0;
        //ESP_LOGI(XVC_TAG, "%s received",cmd);
        if (strncmp(cmd, "ge", 2) == 0) {
            ESP_LOGI(XVC_TAG, "getinfo received");
            const char *info = "xvcServer_v1.0:2048\n";
            n = recv(sock, cmd, 6, 0);
            if(n != 6){
                ESP_LOGE(XVC_TAG, "getinfo reading failed: %d received, expect 6",n);
                break;
            }
            send(sock, info, strlen(info), 0);
        } else if (strncmp(cmd, "se", 2) == 0) { //or: else if (strncmp(cmd, "settck:", 7) == 0)
            //uint32_t recv_buf;
            ESP_LOGI(XVC_TAG, "settck received");
            n = recv(sock, cmd, 9, 0);
            if(n != 9){
                ESP_LOGE(XVC_TAG, "settck readinf failed: %d received, expect 9",n);
                break;
            }
            send(sock, cmd+5, 4, 0);  // Echo back
        } else if (strncmp(cmd, "sh", 2) == 0) { //or: else if (strncmp(cmd, "shift:", 6) == 0)
            //ESP_LOGI(XVC_TAG, "shift received");
#if 0
            n = recv(sock, cmd, 4, 0);//read remaining chars "ift:"
            if(n != 4){
                ESP_LOGE(XVC_TAG, "shift reading length failed: %d received, expect 4",n);
                break;
            }

            int len;
            n = recv(sock, &len, 4, 0);
            if(n != 4){
                ESP_LOGE(XVC_TAG, "shift reading length failed: %d received, expect 4",n);
                break;
            }
#else
            xvc_shift_header_t header;

            // 1. Receive all 8 bytes (4 for "ift:" + 4 for length) in one go
            int n = recv(sock, &header, sizeof(header), 0);

            // 2. Check for successful reception of exactly 8 bytes
            if (n != sizeof(header)) {
                ESP_LOGE(XVC_TAG, "shift reading header failed: %d received, expected %zu", n, sizeof(header));
                break;
            }

            // 3. Verify the signature ("ift:")
            if (memcmp(header.signature, "ift:", 4) != 0) {
                ESP_LOGE(XVC_TAG, "shift header signature mismatch. Expected \"ift:\", got: %.4s", header.signature);
                break;
            }

            // 4. Extract and handle byte order (Endianness) for the integer 'len'
            int len = header.length;
#endif
            int nr_bytes = (len + 7) / 8;
            if ((nr_bytes * 2) > XVC_BUF_SIZE) {
                ESP_LOGE(XVC_TAG, "buffer size exceeded, nr_bytes*2 = %d, > %d", nr_bytes*2, XVC_BUF_SIZE);
                break;
            }
            n = recv_all(sock, buffer, nr_bytes * 2);

            if ((nr_bytes * 2) != n) {
                ESP_LOGE(XVC_TAG, "reading data failed nr_bytes = %d, n=%d", nr_bytes, n);
                break;
            }
            memset(result, 0, nr_bytes);
#if 0
            //Debug
            static uint32_t cnt2=0;
            if(cnt2<200){
                cnt2++;
                ESP_LOGI(XVC_TAG, "\nSH: len of bits/bytes=%d/%d tms/tdi=", len, nr_bytes);
                for(int i=0;i<nr_bytes;++i)
                    printf("%02x ", buffer[i]);
                printf("\n");
                for(int i=0;i<nr_bytes;++i)
                    printf("%02x ", buffer[i+nr_bytes]);
                printf("\n");
            }
#endif
            //reverse bits for SPI. Supposed to shift out LSB first for each byte
            //SPI shift MSB first instead.
            reverse_bits(buffer, nr_bytes * 2);

            //ESP_LOGI(XVC_TAG, "Number of Bits/bytes  : %d / %d", len, nr_bytes);
            //jtag_write(0, 1, 1); //not needed

            uint8_t* tms = buffer;
            uint8_t* tdi = buffer + nr_bytes;
            int ret = proc_spi_transfer(len, tms, tdi, result);
            if(ret<0){
                ESP_LOGE(XVC_TAG, "proc_spi_transfer() failed");
                break;
            }
            //printf("Before reverse_bits:");for(int i=0;i<nr_bytes;++i){printf("%02x ", result[i]);} printf("\n"); //debug
            reverse_bits(result, nr_bytes);
#if 0
            //Debug
            if(cnt2<200){
                //cnt2++;
                ESP_LOGI(XVC_TAG, "len of bits/bytes=%d/%d tdo:", len, nr_bytes);
                for(int i=0;i<nr_bytes;++i)
                    printf("%02x ", result[i]);
                printf("\n");
            }
#endif
            //jtag_write(0, 1, 0);//not needed

            ssize_t sent_total = 0;
            while (sent_total < nr_bytes) {
                ssize_t sent_now = send(sock, result + sent_total, nr_bytes - sent_total, 0);
                if (sent_now <= 0) {
                    ESP_LOGE(XVC_TAG, "send failed (ret=%d, errno=%d)", (int)sent_now, errno);
                    break;
                }
                sent_total += sent_now;
            }

        } else {
            ESP_LOGW(XVC_TAG, "%s received, unknown",cmd);
            //ESP_LOGW(XVC_TAG, "Unknown command received");
            break;
        }
    } //while(true)

    free(buffer);
    free(result);

    ESP_LOGI(XVC_TAG, "Client disconnected");
    close(sock);
}

void xvc_server_task(void *arg) {
    int listen_sock, client_sock;
    struct sockaddr_in server_addr, client_addr;
    socklen_t client_len = sizeof(client_addr);

    listen_sock = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
    if (listen_sock < 0) {
        ESP_LOGE(XVC_TAG, "Unable to create socket");
        vTaskDelete(NULL);
        return;
    }

#if 1    
    extern spi_device_handle_t gbl_spi_h1;
    if(gbl_spi_h1 == NULL){
        esp_err_t ret_spi = spi_master_init();
        if (ret_spi != ESP_OK) {
            ESP_LOGE(XVC_TAG, "SPI init failed: %s", esp_err_to_name(ret_spi));
            //return;
        }
        else{
            ESP_LOGI(XVC_TAG, "SPI init OK!");
            //test_spi();
        }
    }
#endif

    //init_gpio_xvc();  //no need now. If do need to toggle PIN_RESET_N, do it somewhere in main for all related logic 

    //spi2fpga_test_rtl();
    //spi2fpga_test();

    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT);
    server_addr.sin_addr.s_addr = INADDR_ANY;

    bind(listen_sock, (struct sockaddr *)&server_addr, sizeof(server_addr));
    listen(listen_sock, 1);
    ESP_LOGI(XVC_TAG, "XVC server listening on port %d", PORT);

    while (1) {
        client_sock = accept(listen_sock, (struct sockaddr *)&client_addr, &client_len);
        if (client_sock >= 0) {
            ESP_LOGI(XVC_TAG, "Client connected");
            xvc_handle_client_v3(client_sock);
        }
    }

    close(listen_sock);
    vTaskDelete(NULL);
}

#ifdef XVC_USE_SPI
#if 1//debug or test
int spi2fpga_test_rtl(){

    esp_err_t ret1 = 0; 
    uint8_t tx[68]={0xf0};
    uint8_t rx[68]={0};
    for (uint8_t j = 0; j < 68; j = j + 1){
        tx[j]=0xf0;
    }
    //for generatings n bits of TCKs, j=n-1
    //header 1 byte for target_counter=j. Then following k byte of data, k=(j+1)/4+1 or k=n/4+1
    //For each data, bit 7,5,3,1 is for TMS and 6,4,2,0 is for TDO
    //example: 24 to 32 TCKs, n=24 to 32, j= 23 to 31; folowing 7 to 9 bytes
    //n max is 256 or j max is 255
    for (uint16_t j = 0; j <= 255; j = j + 1){
        tx[0] = j;
        int len = ((j+1)/4)+1;
        len++;//header one byte

        ret1 = spi_device3_transfer_data(tx, rx, len);
    }
    return ret1;
}

int spi2fpga_test(){

    uint8_t tms[] = {
        0x03, 0xe0, 0x01, 0x70, 0x00, 0x00, 0x00, 0x00,
        0x16, 0x00, 0x00, 0x0b, 0x00, 0x00, 0x00, 0x00,
        0xd8, 0x00, 0x78, 0x00, 0x1c, 0x00, 0x00, 0x00,
        0x80, 0x05, 0x00, 0xc0, 0x02, 0x00, 0x00, 0x80,
        0x0d, 0x80, 0x07, 0xc0, 0x01, 0x00, 0x00, 0x00,
        0x58, 0x00, 0x00, 0x2c, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x30
    };
    uint8_t tdi[] = {
        0xf0, 0x3f, 0x10, 0x1e, 0x00, 0x03, 0x20, 0x09,
        0x00, 0x00, 0x02, 0x40, 0x2a, 0x02, 0x80, 0x01,
        0x00, 0xfc, 0x0f, 0x84, 0x07, 0xc0, 0x00, 0x48,
        0x02, 0x00, 0xa0, 0x00, 0x90, 0x0a, 0xff, 0x1f,
        0xc0, 0xff, 0x40, 0x78, 0x00, 0x0c, 0x80, 0x24,
        0x00, 0x00, 0x0c, 0x00, 0xa9, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00
    };
    int ret = 0;
    uint8_t result[68] = {0};
    int len_bits =  293;
    ret = proc_spi_transfer(len_bits, tms, tdi, result);
    return ret;
}
#endif
// #define PIN_RESET_N  (12)
// #define PIN_SPI_OR_GPIO (40)
// 
// #define JTAG_SEL_N_PIN (41)
// #define RECONFIG_N_PIN (14)

// void init_gpio_xvc(void) {
//     gpio_config_t io_conf = {
//         .mode = GPIO_MODE_OUTPUT,
//         .pin_bit_mask = (1ULL << PIN_RESET_N) | (1ULL << PIN_SPI_OR_GPIO) | (1ULL << JTAG_SEL_N_PIN),// | (1ULL << FPGA_TCK_PIN) ,
//         .pull_down_en = 0,
//         .pull_up_en = 0,
//         .intr_type = GPIO_INTR_DISABLE
//     };
//     gpio_config(&io_conf);
// 
// //    //gpio_set_direction(TDO, GPIO_MODE_INPUT);
// //    io_conf.mode = GPIO_MODE_INPUT;
// //    io_conf.pin_bit_mask = (1ULL << PIN_GPIO_41);
// //    gpio_config(&io_conf);
// 
//     gpio_set_level(JTAG_SEL_N_PIN, 1);//FPGA JTAG PINs used as GPIO and not as JTAG PINS
// 
//     gpio_set_level(PIN_SPI_OR_GPIO, 1);//SPI and not GPIO bitbang
// 
//     //reset on-board Gowin FPGA
//     gpio_set_level(PIN_RESET_N, 0);
//     for(int i =0;i<256;++i) {} //delay some time
//     gpio_set_level(PIN_RESET_N, 1);
// }
#else //ndef XVC_USE_SPI //GPIO version
void init_gpio_xvc(void) {
    gpio_config_t io_conf = {
        .mode = GPIO_MODE_OUTPUT,
        .pin_bit_mask = (1ULL << TCK) | (1ULL << TMS) | (1ULL << TDI),
        .pull_down_en = 0,
        .pull_up_en = 0,
        .intr_type = GPIO_INTR_DISABLE
    };
    gpio_config(&io_conf);

    //gpio_set_direction(TDO, GPIO_MODE_INPUT);
    io_conf.mode = GPIO_MODE_INPUT;
    io_conf.pin_bit_mask = (1ULL << TDO);
    gpio_config(&io_conf);
}
#endif

