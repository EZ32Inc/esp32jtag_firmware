# ESP32-S3 DevKit Black Magic Debug Support

## Scope

This note documents the current `esp32s3_devkit` board profile in
`esp32jtag_firmware`.

The goal of this profile is to enable generic ESP32-S3 dev boards to run the
Black Magic Probe debug functions without requiring the ESP32JTAG product
hardware.

This is not a full replacement for the ESP32JTAG board. It is a reduced board
profile focused on GPIO-based SWD/JTAG debug.

## Build Target

Select the board profile:

- `CONFIG_AEL_BOARD_ESP32S3_DEVKIT=y`

This profile keeps the firmware and BMP network service, but disables product-
specific hardware features that depend on the ESP32JTAG FPGA and related board
circuitry.

## Supported Debug Pins

Current `esp32s3_devkit` GPIO mapping:

- `SWCLK` -> `GPIO4`
- `SWDIO` -> `GPIO5`
- `TDI` -> `GPIO7`
- `TDO` -> `GPIO15`

Important difference from the ESP32JTAG board:

- `SWDIO_RDnWR` is **not used** on the generic ESP32-S3 devkit
- the old ESP32JTAG FPGA-oriented `GPIO6` turnaround control does **not** apply
  to this board profile
- SWDIO on the devkit is wired directly to the ESP32-S3 MCU GPIO

## Wiring For Basic SWD

Minimum SWD wiring:

- ESP32-S3 `GPIO4` -> target `SWCLK`
- ESP32-S3 `GPIO5` -> target `SWDIO`
- ESP32-S3 `GND` -> target `GND`

Recommended:

- share target power reference and confirm target IO voltage is compatible with
  the board wiring
- keep SWD wiring short
- if reset control is needed later, add a board-specific NRST mapping rather
  than reusing ESP32JTAG assumptions

## Runtime Access

Validated access path:

- device runs in AP mode
- AP address: `192.168.4.1`
- BMP GDB server: TCP `4242`

Example GDB session:

```gdb
(gdb) target extended-remote 192.168.4.1:4242
(gdb) monitor swd_scan
(gdb) monitor targets
```

## Default SWD Frequency

The `esp32s3_devkit` profile uses a conservative default SWD frequency policy.

Current behavior:

- board default frequency is set to `100000 Hz`
- SWD scan path reapplies the board default immediately before scanning

This matters because on the validated devkit wiring, probe operation was not
reliable unless the frequency was explicitly reduced before scan. The scan path
now applies the safe board default automatically, so manual `monitor frequency
100k` is no longer required for this board profile.

## Validated Result

Live validation completed with:

- firmware built for `esp32s3_devkit`
- firmware flashed over native USB on ESP32-S3
- host connected to the device AP
- BMP GDB connected to `192.168.4.1:4242`
- target connected over SWD

Observed successful target detection:

- `RP2040 M0+`
- `RP2040 Rescue (Attach to reset)`

So the first milestone is complete:

- generic ESP32-S3 dev board can run Black Magic SWD debug
- target scan works on real hardware
- ESP32JTAG original board build still passes

## Current Limitations

The `esp32s3_devkit` board profile does **not** currently provide the full
ESP32JTAG product feature set.

Unsupported or intentionally disabled in this profile:

- FPGA-backed data acquisition
- logic analyzer capture path
- FPGA XVC path
- LCD features
- product-specific target-voltage PWM control
- ESP32JTAG-specific multi-port routing assumptions

Notes:

- Wi-Fi AP reconnect on the host side may need to be done manually after some
  flash/reset cycles
- the validated path so far is SWD; JTAG on the devkit profile is only pin-
  mapped at this stage and should be treated as not yet validated until tested
  on hardware

## Design Notes

The generic devkit support depends on two key differences relative to the
ESP32JTAG board:

1. board-specific pin/capability selection is done through board profiles
2. direct-GPIO SWD is used on the devkit, without the ESP32JTAG-specific
   `SWDIO_RDnWR` control signal

This separation is required so that:

- `ESP32JTAG` continues to work as before
- generic ESP32-S3 boards can support BMP debug without inheriting FPGA-only
  hardware assumptions

## Next Suggested Work

After SWD scan bring-up, likely next steps are:

- validate halt / attach / register access on a real target
- validate flash programming on a real target
- add optional target reset mapping for the devkit profile
- document or automate host Wi-Fi reconnect during validation flows
- add a dedicated AEL smoke path for `esp32s3_devkit`
