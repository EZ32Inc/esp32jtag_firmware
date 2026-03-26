## Board Support Refactor Plan

Date: 2026-03-25

### Goal

Extend `esp32jtag_firmware` so it is no longer tied to only the ESP32JTAG
hardware product.

First target:

- keep `ESP32JTAG` behavior working as before
- add a generic `ESP32-S3 DevKit` build target
- enable Black Magic debug on the generic board
- do not require FPGA, LCD, logic-analyzer, or target-voltage-control hardware
  on the generic board

Future targets:

- ESP32-C5 boards
- ESP32-C6 boards

### Current Problem

The firmware currently mixes two different concerns:

1. common probe/application logic
2. ESP32JTAG-specific hardware assumptions

Those assumptions are spread across:

- `main/esp32jtag_common.h`
- `main/port_cfg.h`
- `main/main.c`
- `components/blackmagic_esp32/src/platforms/esp32/main/platform.h`

Examples of product-specific assumptions:

- FPGA bitstream loading
- LCD startup and LCD status drawing
- FPGA-backed port muxing and capture routing
- target-voltage PWM control
- ESP32JTAG-specific GPIO assignment for BMP SWD/JTAG pins

### First-Pass Architecture

Split support into two layers:

1. firmware-internal board profiles
2. AEL-side board configs and smoke tests

For the firmware project, introduce explicit board selection and move hardware
facts behind a board profile.

### Firmware Design

Add a board-selection layer driven by build config:

- `esp32jtag_s3`
- `esp32s3_devkit`

Each board profile should define:

- board identity
- GPIO mapping for Black Magic SWD/JTAG
- feature/capability flags
- default port configuration values
- optional hardware support flags

Expected capability flags:

- `has_fpga`
- `has_lcd`
- `has_target_vio_pwm`
- `has_logic_analyzer`
- `has_xvc`
- `has_secondary_button`

### Refactor Boundaries

Common functionality:

- Wi-Fi/network stack
- GDB server
- Black Magic platform integration
- GPIO-based SWD/JTAG
- NVS/config storage
- general runtime logging

Board-specific functionality:

- FPGA load/config
- FPGA SPI buses and port muxing
- LCD usage
- target-voltage PWM
- FPGA-backed XVC
- FPGA-backed logic-analyzer behavior

### Required Changes

1. Add build-time board selection in Kconfig.
2. Add a shared board-profile header for both app code and Black Magic ESP32
   platform code.
3. Replace hardcoded `ESP32JTAG` pin and feature assumptions with board-profile
   macros.
4. Preserve the current `ESP32JTAG` behavior as the default board.
5. Add `ESP32-S3 DevKit` board profile with:
   - Black Magic pin mapping
   - no FPGA
   - no LCD
   - no target-voltage PWM
   - no logic analyzer
   - no XVC
6. Capability-gate app paths so unsupported hardware returns cleanly instead of
   crashing or assuming product-only peripherals exist.

### First Implementation Milestone

The first milestone is not "port the whole product".

It is:

- make the firmware compile for both board targets
- preserve current ESP32JTAG behavior
- enable generic ESP32-S3 Black Magic debug build without product-only
  hardware assumptions

### App Code Gating Needed

The following paths must be capability-gated first:

- `load_fpga()`
- `ICE_Init()`
- `spi_master_init()` for FPGA-backed buses
- `set_cfga()`
- `set_la_input_sel()`
- `set_sreset()`
- Port D FPGA output helpers
- LCD init and LCD drawing paths
- target-voltage PWM setup
- logic-analyzer startup
- XVC startup

### AEL Side

After firmware build support is in place:

1. keep the existing `ESP32JTAG` AEL board config
2. add a new AEL board config for the generic ESP32-S3 probe board
3. add a separate smoke test for the generic board

Do not reuse the current smoke expectations unchanged because they currently
expect product-specific boot messages such as FPGA readiness.

### Build Strategy

Keep `esp32jtag_s3` as the default board so existing brownfield AEL flows keep
working without immediate config churn.

Use small board-specific defaults files to build alternate board targets
non-interactively.

### Success Criteria

- `ESP32JTAG` still builds and keeps existing features enabled
- `ESP32-S3 DevKit` builds successfully
- generic board runtime does not require FPGA/LCD paths
- Black Magic pin selection is board-specific
- no existing product-only feature is silently removed from `ESP32JTAG`
