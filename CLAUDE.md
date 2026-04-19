# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

Universal CAN bus data logger. Reads CAN frames, maps them to parameters via config file on SD, writes MLVLG v2 binary logs (compatible with MegaLogViewer). Config-driven — no reflashing needed for different vehicles/protocols.

**Current stage: v1.0 in progress.** Full E2E verified: Nissan ECU → cansult → CAN → canlogger → SD → MegaLogViewer. FreeRTOS CMSIS_V2: `task_producer` + `task_sd` (SD GC stalls isolated to task_sd, see `docs/SD_WRITER_DECOUPLING.md`). AEM UEGO 30-0300 supported via 29-bit extended CAN. RTC persists across reset/power-loss (LSE + CR1220 + INITS flag). Persistent fault log in BKP regs (CDC `lastfault`). Graceful shutdown firmware ready (VIN_SENSE ADC + armed debounce), awaits hat with supercap.

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
│   ├── main.c    # Init, FreeRTOS tasks (task_producer + task_sd), glue
│   ├── can_drv.c # CAN HAL → ring_buf (ISR callback)
│   ├── log_writer.c # SD: read config, write MLG (LFN names: 2026-04-12_HH-MM-SS_NN.mlg), LED, error handling, SD mount retry
│   ├── sd_write_dma.c # BSP override: DMA write fix (CubeMX-safe)
│   ├── vin_sense.c # VIN ADC polling + graceful shutdown (armed debounce)
│   ├── bkp_log.c # Persistent fault log in RTC backup registers (session counter + last fault, survives reset/power loss)
│   ├── debug_out.c # USB CDC CLI (help/status/stream/config/ls/get/put/settime/lastfault/fault/stop), echo, buffered printf
│   ├── freertos.c # FreeRTOS hooks (CubeMX generated, USER CODE sections)
│   ├── usbd_*.c  # USB Device CDC (CubeMX generated)
│   └── usb_device.c # USB Device init (CubeMX generated)
├── Inc/          # Headers for Src/ (incl. FreeRTOSConfig.h)
├── test/         # Host unit tests (Unity framework)
│   ├── unity/
│   ├── snapshots/ # Etalon .mlg files
│   └── config.ini # Reference config (cansult + AEM UEGO 30-0300) for E2E testing
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
make build        # CubeIDE headless build → Debug/canlogger.elf
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
- **FreeRTOS** — CMSIS_V2, heap_4 (16 KB). Two tasks: `task_producer` (CAN + debug, osPriorityNormal, 2 KB stack) and `task_sd` (SD writer, osPriorityBelowNormal, 4 KB stack). Shadow buffer `field_values` guarded by `shadow_mutex`. HAL timebase on TIM6 (SysTick owned by FreeRTOS). All `HAL_Delay` converted to `osDelay` in Src/.
- **RAM layout** — `config` (52.7 KB) in CCM SRAM via `__attribute__((section(".ccmram")))`; `mlg_fields` removed (MLG header built on-the-fly from `cfg_Config`); `ring_Buffer` (64 KB) in main SRAM. Main SRAM ~54/128 KB used, CCM ~53/64 KB used.
- **MLVLG v2** — all data big-endian. `DisplayValue = (rawValue + transform) * scale`
- **Config** — INI-like `config.ini` on SD card. See `docs/CONFIG_GUIDE.md` and `docs/ARCHITECTURE.md` for format spec. Supports extended (29-bit) CAN IDs via explicit `is_extended = 1` (AEMnet 0x180) and sub-byte (1–7 bit) fields via `start_bit` + `bit_length` (ECU status bits on 0x668).
- **RTC** — LSE (32.768 kHz Y2 crystal) + CR1220 on VBAT. Persists across reset/flash via `RTC_FLAG_INITS` check. Set via CDC `settime YYYY-MM-DD HH:MM:SS` or GPS (TODO). File names: `2026-04-12_HH-MM-SS_NN.mlg` (LFN), suffix `_NN` avoids collisions via `FA_CREATE_NEW`.
- **FatFS** — `_USE_LFN = 2` (stack buffer), long filenames enabled. Single-task access (task_sd only), `_FS_REENTRANT = 0`.
- **BKP registers** — `Src/bkp_log.c`: DR1 session counter, DR2 fault session, DR3 packed fault info. Persistent across reset/power loss while VBAT alive. CDC `lastfault`.
- **BOR Level 3** (2.70V) in Option Bytes — MCU held in reset until Vdd stable, avoids bad SD init on slow supply rise.

## Hardware

See `docs/HARDWARE.md` for full pin assignments, SWD wiring, LED states, hat design.

- MCU: STM32F407VET6, 168MHz. Board: STM32_F4VE V2.0
- SD: SDIO 4-bit mode, FatFS
- CAN1: PB8 (RX), PB9 (TX) — configurable bitrate (default 500 kbit/s)
- CAN input: modified MCP2515 board (TJA1050 transceiver only, SO→TX, SI→RX)
- USB: PA11/PA12 — USB CDC debug output (Virtual COM Port, `/dev/cu.usbmodemXXXX`)
- LEDs: PA6 (D2), PA7 (D3), active-low. Buttons: PE3/K1 (shutdown), PE4/K0 (MLG marker)
- E2E test source: `../cansult` (Nissan Consult → CAN, 3 messages @ 20Hz)
- Known issue: cansult UART↔ECU connection drops after some time
