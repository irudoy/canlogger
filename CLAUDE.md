# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

Universal CAN bus data logger. Reads CAN frames, maps them to parameters via config file on SD, writes MLVLG v2 binary logs (compatible with MegaLogViewer). Config-driven — no reflashing needed for different vehicles/protocols.

**Current stage: MVP + debug output.** Full E2E verified: Nissan ECU → cansult → CAN → canlogger → SD → MegaLogViewer with real Battery/Coolant/TPS data. USB CDC debug output working (printf over USB Virtual COM Port).

Full requirements and roadmap: `docs/REQUIREMENTS.md`
Architecture and module design: `docs/ARCHITECTURE.md`
Hardware and wiring: `docs/HARDWARE.md`
Debug USB CDC output: `docs/DEBUG.md`
Schematic conventions: `docs/SCHEMATIC_CONVENTIONS.md`

## Repository Structure

```
firmware/
├── Lib/          # Pure business logic (no HAL), host-testable
│   ├── mlvlg.*   # MLVLG v2 encoder (done, 17 unit + 4 snapshot tests)
│   ├── config.*  # INI config parser + LUT (done, 22 tests)
│   ├── can_map.* # CAN → field values mapper + LUT interpolation (done, 17 tests)
│   ├── demo_gen.*# Demo data generator: sine/ramp/square/noise/const (done, 10 tests)
│   ├── cfg_limits.h # Shared limits (CFG_MAX_FIELDS, CFG_MAX_CAN_IDS)
│   └── ring_buf.*# SPSC ring buffer for CAN frames (done, 7 tests)
├── Src/          # HAL wrappers
│   ├── main.c    # Init, main loop, glue
│   ├── can_drv.c # CAN HAL → ring_buf (ISR callback)
│   ├── log_writer.c # SD: read config, write MLG, LED, error handling
│   ├── sd_write_dma.c # BSP override: DMA write fix (CubeMX-safe)
│   ├── debug_out.c # USB CDC CLI (help/status/stream/config/ls/get/put), echo, buffered printf
│   ├── usbd_*.c  # USB Device CDC (CubeMX generated)
│   └── usb_device.c # USB Device init (CubeMX generated)
├── Inc/          # Headers for Src/
├── test/         # Host unit tests (Unity framework)
│   ├── unity/
│   ├── snapshots/ # Etalon .mlg files
│   └── cansult_config.ini # Reference config for E2E testing
├── mlg-test/     # Node.js .mlg file validator
└── Makefile      # Build/test/flash/debug commands
docs/
├── REQUIREMENTS.md  # Vision, roadmap
├── ARCHITECTURE.md  # Modules, data flow, config format, test strategy
├── HARDWARE.md      # Board, pins, SWD wiring, hat schematic
└── reference/       # MLVLG specs (PDFs), board schematic, rusefi sources
```

## Commands (from `firmware/`)

```bash
make build        # RTC time gen + CubeIDE headless build → Debug/canlogger.elf
make clean        # Clean rebuild
make test         # Run host unit tests (Unity)
make flash        # Build + flash via ST-Link SWD
make erase        # Full chip erase
make reset        # Hardware reset
make ocd-server   # Start OpenOCD GDB server on :3333
make ocd-debug    # Build + GDB connect via OpenOCD
make ocd-status   # Run 3s, halt, print runtime variables
make gdb-server   # ST-LINK GDB server (requires recent ST-Link firmware)
make debug        # GDB connect via ST-LINK server
make gdb-read EXPRS="var1 var2"  # Read variables via GDB (auto OpenOCD)
make gdb-exec SCRIPT=file.gdb   # Run GDB script (auto OpenOCD)
make cdc-cmd CMD=status          # Send CDC command, read response
make cdc-put FILE=config.ini     # Upload file to SD via CDC
```

Build system: STM32CubeIDE headless (`.project`/`.cproject`).

## Testing

TDD approach. All business logic lives in `Lib/` and is tested on host with native gcc + Unity.

```bash
cd firmware && make test
```

- Tests auto-glob all `Lib/*.c` sources
- Test files: `test/test_*.c`
- Snapshot tests: `test/snapshots/` (etalon .mlg files for wire format regression)
- Integration validation: `mlg-test/` (Node.js mlg-converter parser)

## Key Conventions

- **Lib/** — no HAL includes, no CubeMX markers. Modules communicate via plain C structs.
- **Src/** — CubeMX `USER CODE BEGIN/END` markers. Custom code must stay within markers.
- **MLVLG v2** — all data big-endian. `DisplayValue = (rawValue + transform) * scale`
- **Config** — INI-like `config.ini` on SD card. See `docs/ARCHITECTURE.md` for format spec.

## Hardware

See `docs/HARDWARE.md` for full pin assignments, SWD wiring, LED states, hat design.

- MCU: STM32F407VET6, 168MHz. Board: STM32_F4VE V2.0
- SD: SDIO 4-bit mode, FatFS
- CAN1: PB8 (RX), PB9 (TX) — configurable bitrate (default 500 kbit/s)
- CAN input: modified MCP2515 board (TJA1050 transceiver only, SO→TX, SI→RX)
- USB: PA11/PA12 — USB CDC debug output (Virtual COM Port, `/dev/cu.usbmodemXXXX`)
- LEDs: PA6 (D2), PA7 (D3), active-low. Button: PE3/K1 (shutdown)
- E2E test source: `../cansult` (Nissan Consult → CAN, 3 messages @ 20Hz)
- Known issue: cansult UART↔ECU connection drops after some time
