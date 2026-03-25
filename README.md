# ESP32-S3 Multi-Function Debug Tool

An all-in-one hardware debugging and development platform built on the ESP32-S3. It combines a JTAG/SWD debugger, 16-channel logic analyzer, FPGA programmer, XVC server, signal generator, and a web-based configuration interface — all accessible over USB or WiFi.

---

## Features

- **JTAG / SWD Debugger**
  - [BlackMagic Probe](https://black-magic.org/) (BMP) — native GDB server
  - [CMSIS-DAP](https://arm-software.github.io/CMSIS_5/DAP/html/index.html) over USB (CherryDAP)
  - [XVC](https://docs.xilinx.com/r/en-US/ug908-vivado-programming-debugging/Virtual-Cable) (Vivado Virtual Cable) for FPGA development

- **16-Channel Logic Analyzer**
  - 264 MHz default sample rate (configurable)
  - Configurable per-channel triggers (rising, falling, crossing, high, low)
  - 128 KB PSRAM capture buffer
  - Web-based viewer

- **FPGA Support (ICE40UP5K)**
  - Automatic bitstream loading on boot
  - Port multiplexing for all debug interfaces
  - SPI and GPIO configuration modes

- **Network**
  - WiFi AP mode (standalone hotspot) or STA mode (connects to existing network)
  - WiFi provisioning
  - OTA firmware updates over HTTPS
  - WebSocket bridge for UART traffic

- **Web Interface**
  - HTTPS web server with embedded TLS certificate
  - Basic-auth protected configuration pages
  - Logic analyzer capture and visualization
  - Debugger target / RTOS / interface selection
  - Port A/B/C/D mode configuration

- **Signal Generation (Port D)**
  - Provides signal stimulus to the target system via Port D pins
  - FPGA internal free-running counter output: bits [3:0] or [7:4] at 132 MHz
  - Software square wave: 125 / 250 / 500 / 1000 Hz on all four Port D pins (~50% duty)
  - Useful for clock injection, loopback self-test, and functional excitation of target circuits
  - Applied instantly via web UI — no reboot required

- **UART / USB**
  - USB CDC or WebSocket-based UART bridge
  - Configurable baud rate, data bits, stop bits, parity

---

## Hardware

| Item | Value |
|---|---|
| MCU | ESP32-S3 |
| Flash | 8 MB |
| PSRAM | Required (logic analyzer buffer) |
| FPGA | Lattice ICE40UP5K |
| Display | Optional LCD (SPI3) |

### Pin Map

| Signal | GPIO |
|---|---|
| UART TX | 43 |
| UART RX | 44 |
| SPI CLK | 38 |
| SPI MOSI | 14 |
| SPI MISO | 39 |
| SPI CS0 (manual) | 21 |
| SPI CS1 | 13 |
| SPI CS2 | 11 |
| SPI CS3 | 5 |
| SWDIO | 41 |
| SWDIO RD/nWR | 45 |
| Button SW1 | 0 |
| Button SW2 | 48 |

---

## Port Modes

The device has four logical ports (A–D). Each port is configured independently via the web UI or NVS storage.

| Port | Available Modes |
|---|---|
| **A** | Logic Analyzer · BMP SWD · BMP JTAG |
| **B** | Logic Analyzer · UART + Reset + Target Voltage |
| **C** | Logic Analyzer · BMP SWD/JTAG · FPGA JTAG config · FPGA SPI config |
| **D** | Logic Analyzer · FPGA XVC · Signal Generation (Counter Lo/Hi @ 132 MHz, GPIO Direct 125/250/500/1000 Hz) |

---

## Getting Started

### Prerequisites

- [ESP-IDF v5.0 or newer](https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/get-started/)
- Python 3.8+
- Git

### Clone

```bash
git clone --recursive https://github.com/<your-org>/esp32jtag.git
cd esp32jtag
```

### Build

```bash
. $IDF_PATH/export.sh
idf.py build
```

### Flash

```bash
idf.py -p /dev/ttyUSB0 flash monitor
```

Or merge into a single binary for mass programming:

```bash
cd build
esptool.py --chip esp32s3 merge_bin -o esp32jtag_merged.bin @flash_args
esptool.py --chip esp32s3 -p /dev/ttyUSB0 write_flash 0x0 esp32jtag_merged.bin
```

---

## First Boot

On first boot the device starts in **WiFi AP mode**.

| Setting | Default |
|---|---|
| SSID | `esp32jtag` |
| Password | `esp32jtag` |
| Web UI | `https://192.168.4.1` |
| Web username | `admin` |
| Web password | `admin` |

> The device uses a self-signed TLS certificate. Accept the browser security warning on first visit.

---

## Web Interface

After connecting to the AP (or the local network in STA mode), open a browser and navigate to the device IP.

| Path | Description |
|---|---|
| `/` | Main dashboard — port configuration, debugger settings |
| `/loganalyzer` | Logic analyzer capture and waveform viewer |
| `/help` | User guide |
| `/credentials` | Change web UI username and password |
| `/ota_upload` | Upload new firmware |
| `/reset_to_factory` | Erase all NVS settings and reboot |

### Debugger Configuration

From the main dashboard you can select:

- **Target** — target chip configuration file
- **Interface** — JTAG or SWD
- **RTOS** — FreeRTOS or none (for RTOS-aware stack unwinding)
- **Dual Core** — enable SMP debugging
- **Flash Support** — enable flash programming
- **Debug Level** — verbosity (0–4)

Press **Run** to save and reboot with the new configuration.

---

## Debugging

### BlackMagic Probe (BMP)

BMP exposes a native GDB server directly on the device.

```bash
arm-none-eabi-gdb firmware.elf
(gdb) target extended-remote /dev/ttyACM0
(gdb) monitor swdp_scan
(gdb) attach 1
```

### CMSIS-DAP (USB)

When connected via USB, the device appears as a CMSIS-DAP interface compatible with pyOCD and most IDEs (VS Code Cortex-Debug, Keil, IAR).

### XVC (Vivado Virtual Cable)

For FPGA development, configure Port D to **XVC** mode. Then connect Vivado to `<device-ip>:2542`.

---

## Logic Analyzer

1. Open `https://<device-ip>/loganalyzer`
2. Configure sample rate and channel triggers
3. Click **Start Capture** (triggered) or **Instant Capture**
4. The waveform appears in the browser once capture completes

**Trigger types:** rising edge · falling edge · crossing · high level · low level

**Buffer:** 128 KB at up to 264 MHz → ~500 µs full-speed capture window

---

## FPGA

The ICE40UP5K FPGA is the signal-routing backbone of the device. It handles:

- Logic analyzer signal capture (16 channels)
- Protocol multiplexing across ports A–D
- Reset signal generation
- JTAG/SWD signal path selection

The bitstream (`main/ice40up5k/bitstream.bin`) is embedded in the firmware and loaded automatically at startup. To update the bitstream, replace the file and rebuild.

---

## Partition Table

| Name | Type | Offset | Size |
|---|---|---|---|
| NVS | data/nvs | 0x9000 | 24 KB |
| OTA data | data/ota | 0xf000 | 8 KB |
| PHY init | data/phy | 0x11000 | 4 KB |
| factory | app/factory | 0x20000 | 5 MB |
| ota_0 | app/ota_0 | — | 2 MB |
| ota_1 | app/ota_1 | — | 2 MB |
| storage (FAT) | data/fat | — | 528 KB |

---

## NVS Keys Reference

| Key | Description |
|---|---|
| `ssid` / `pass` | WiFi STA credentials |
| `ap_ssid` / `ap_pass` | WiFi AP credentials |
| `wifi_mode` | `ap` or `sta` |
| `file` | Target config file |
| `rtos` | RTOS type |
| `smp` | Dual-core enable (`1` or `3`) |
| `flash` | Flash support (`auto` or `0`) |
| `interface` | `0`=JTAG, `1`=SWD |
| `debug` | Debug level (`1`–`5`) |
| `pa_cfg` … `pd_cfg` | Port A–D mode |
| `uart_baud` | UART baud rate |
| `uart_psel` | UART port (`0`=USB CDC, `1`=WebSocket) |
| `dis_usb_dap` | Disable USB DAP interface |

---

## Build Configuration

Key `sdkconfig` options:

| Option | Value |
|---|---|
| Target | `esp32s3` |
| Flash size | 8 MB |
| Flash frequency | 80 MHz |
| CPU frequency | 240 MHz |
| SPIRAM | Enabled (80 MHz) |
| Main task stack | 32 KB |
| Watchdog timeout | 15 s |
| UI | Enabled via `CONFIG_UI_ENABLE` |

---

## Project Structure

```
esp32jtag/
├── main/
│   ├── main.c                  # Application entry point
│   ├── types.h                 # Shared type definitions and NVS keys
│   ├── esp32jtag_common.h      # Pin definitions and port enums
│   ├── storage.c               # NVS read/write helpers
│   ├── ui.c / ui_events.c      # LVGL display (optional)
│   ├── ice40up5k/
│   │   ├── ice.c               # FPGA SPI driver
│   │   └── bitstream.bin       # ICE40UP5K bitstream (embedded)
│   ├── network/
│   │   ├── web_server.c        # HTTP/HTTPS handlers
│   │   ├── network_mngr.c      # WiFi AP/STA management
│   │   ├── network_mngr_ota.c  # OTA update handler
│   │   ├── uart_websocket.c    # WebSocket ↔ UART bridge
│   │   └── descriptors.c       # Port configuration descriptors
│   └── web/                    # Embedded HTML assets
├── components/
│   ├── blackmagic_esp32/       # BlackMagic Probe (submodule)
│   ├── CherryDAP/              # CMSIS-DAP over USB
│   ├── lcd/                    # LCD display driver
│   └── platform_include/       # POSIX shims for host headers
├── CMakeLists.txt
├── partitions_ota_2mb.csv
└── sdkconfig.defaults
```

---

## License

Apache License 2.0 — see [LICENSE](LICENSE) for details.

Third-party components retain their own licenses:
- **BlackMagic Probe** — GPL-3.0
- **CherryUSB / CherryDAP** — Apache-2.0
