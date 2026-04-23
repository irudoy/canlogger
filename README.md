# CANLogger

Standalone universal CAN bus logger. Reads frames from a vehicle CAN bus, packs them into MLVLG v2 format and writes to an SD card. Logs open in [MegaLogViewer](https://www.efianalytics.com/MegaLogViewer/) and [UltraLog](https://ultralog.co/).

Two ideas drive the design:

- **Universality.** The device doesn't know ahead of time which IDs and fields it's going to log. The CAN → MLG mapping is defined by a `config.ini` file on the SD card, so a single firmware image works with different cars and protocols without recompilation.
- **DIY-friendliness.** An off-the-shelf STM32_F4VE board from AliExpress already provides the MCU, USB, SD slot, LEDs and buttons. Everything CAN-specific (transceiver, DC-DC, supercap, shutdown circuit, connectors) lives on a hat that can be built on a prototyping board or ordered as a PCB — all from standard, widely available components.

## Hardware

- STM32F407VET6 @ 168 MHz (STM32_F4VE V2.0 board, off-the-shelf)
- CAN1 + TJA1050 / MCP255x transceiver, 125k / 250k / 500k / 1M
- SD card over SDIO 4-bit + FatFS
- USB CDC for config upload, diagnostics, and log download without ejecting the SD
- Buttons: K0 (MLG marker), K1 (graceful shutdown)
- Hat with CAN transceiver, DC-DC and supercap for a clean flush on power loss

Details: [docs/HARDWARE.md](docs/HARDWARE.md).

## Features

- Standard and extended (29-bit) CAN IDs
- U08/S08/U16/S16/U32/S32/S64/F32, scale/offset, LUT, sub-byte fields
- Plausibility filter with `last_good` / `clamp` / `skip` strategies and sensor presets (`aem_uego`)
- MLG v2 with markers and a synthetic Date field (Unix timestamp)
- LSE-backed RTC with VBAT battery — time set over USB CDC, survives resets
- Graceful shutdown on VIN drop: flush → f_sync → f_close, auto-resumes a new session after reset
- Recovery from SD errors: truncate → sync → close → remount → new file, without stopping logging
- Persistent fault log in RTC backup registers — the last fault is visible after reboot

## Quick Start

1. Build the hardware per [HARDWARE.md](docs/HARDWARE.md)
2. Flash the firmware (see `firmware/` and `make flash`)
3. Put `config.ini` in the FAT32 SD card root — examples live in `firmware/test/`
4. Insert the SD, power up — LED D2 on = logging
5. Power off (or press K1), remove the SD, open `.mlg` files in MegaLogViewer / UltraLog

USB CDC (`/dev/cu.usbmodem…` on macOS, `/dev/ttyACM…` on Linux) supports `help`, `status`, `stream`, `ls`, `get`, `mark`, `settime`. See [docs/DEBUG.md](docs/DEBUG.md).

## Config Format

```ini
[logger]
interval_ms = 50          # 20 Hz snapshot rate
can_bitrate = 500000

[field]
can_id = 0x666
name = Coolant
units = C
start_byte = 1
bit_length = 8
type = U08
scale = 1.0
offset = -50.0
category = Engine
```

Full reference: [docs/CONFIG_GUIDE.md](docs/CONFIG_GUIDE.md).

## Repository Layout

- `firmware/Lib/` — pure C, host-tested with Unity, no HAL
- `firmware/Src/` — HAL glue and CubeMX-generated code, edits only inside `USER CODE` sections
- `firmware/test/` — host tests + config examples
- `firmware/mlg-test/` — Node.js MLG validator
- `hardware/` — KiCad projects for the hat and schematics
- `docs/` — project documentation (see [docs/README.md](docs/README.md))

## Build and Test

Prerequisites:

- [STM32CubeIDE](https://www.st.com/en/development-tools/stm32cubeide.html) — used headless for building and for the bundled toolchain (arm-none-eabi-gcc, ST-LINK GDB server, STM32_Programmer_CLI)
- External ST-LINK/V2 dongle — the STM32_F4VE has a 20-pin JTAG/SWD header (P1), no onboard programmer
- `gcc` + `python3` for host-side unit tests and CDC scripts (macOS: via Xcode CLT / brew)

All commands run from `firmware/`:

```
make build     # headless STM32CubeIDE build (Debug)
make test      # host unit tests (native gcc + Unity)
make flash     # flash the board over ST-LINK
make erase     # erase flash
make reset     # reset the MCU
make cdc-cmd CMD=status        # send a CLI command over USB CDC
make cdc-put FILE=config.ini   # upload a file to the SD over USB CDC
```

Debugging targets (`ocd-server`, `ocd-debug`, `gdb-server`, `debug`, `gdb-read`) are documented in [docs/DEBUG.md](docs/DEBUG.md).

## Status

Firmware is working — it logs a Nissan ECU via the [cansult](https://github.com/irudoy/cansult) bridge and an AEM X-Series UEGO lambda controller. Hat PCB is in progress (breadboard prototype validated). Current roadmap and open tasks live in [docs/REQUIREMENTS.md](docs/REQUIREMENTS.md).
