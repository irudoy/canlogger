# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

Universal CAN bus data logger. Reads CAN frames, maps them to parameters via config file on SD, writes MLVLG v2 binary logs (compatible with MegaLogViewer). Config-driven — no reflashing needed for different vehicles/protocols.

**Current stage: PoC complete, working on MVP.** MLVLG encoder works end-to-end (host tests → SD → MegaLogViewer). Next: CAN reception + config-driven field mapping.

Full requirements and roadmap: `docs/REQUIREMENTS.md`
Architecture and module design: `docs/ARCHITECTURE.md`

## Repository Structure

```
firmware/
├── Lib/          # Pure business logic (no HAL), host-testable
│   ├── mlvlg.*   # MLVLG v2 encoder (done, 17 unit + 4 snapshot tests)
│   ├── config.*  # INI config parser (planned)
│   ├── can_map.* # CAN → field values mapper (planned)
│   └── ring_buf.*# Lock-free SPSC ring buffer (planned)
├── Src/          # HAL wrappers (thin, no business logic)
├── Inc/          # Headers for Src/
├── test/         # Host unit tests (Unity framework)
│   └── unity/    # Unity test framework
├── mlg-test/     # Node.js .mlg file validator
└── Makefile      # Build/test/flash/debug commands
docs/
├── REQUIREMENTS.md  # Vision, hardware, roadmap
├── ARCHITECTURE.md  # Modules, data flow, config format, test strategy
└── reference/       # MLVLG specs (PDFs), board schematic, rusefi sources
```

## Commands (from `firmware/`)

```bash
make build       # RTC time gen + CubeIDE headless build → Debug/canlogger.elf
make clean       # Clean rebuild
make test        # Run host unit tests (Unity)
make flash       # Build + flash via ST-Link SWD
make erase       # Full chip erase
make reset       # Hardware reset
make gdb-server  # Start ST-LINK GDB server on :3333
make debug       # Build + GDB connect + load (run gdb-server in another terminal first)
```

Build system: STM32CubeIDE headless (`.project`/`.cproject`). Legacy `CMakeLists.txt` exists for CLion.

## Testing

TDD approach. All business logic lives in `Lib/` and is tested on host with native gcc + Unity.

```bash
cd firmware && make test
```

- Tests auto-glob all `Lib/*.c` sources
- Test files: `test/test_*.c`
- Snapshot tests: `test/snapshots/` (etalon binary buffers for regression)
- Integration validation: `mlg-test/` (Node.js mlg-converter parser)

## Key Conventions

- **Lib/** — no HAL includes, no CubeMX markers. Modules communicate via plain C structs.
- **Src/** — CubeMX `USER CODE BEGIN/END` markers. Custom code must stay within markers.
- **MLVLG v2** — all data big-endian. `DisplayValue = (rawValue + transform) * scale`
- **Config** — INI-like format on SD card. See `docs/ARCHITECTURE.md` for spec.

## Hardware

See `docs/HARDWARE.md` for full pin assignments, SWD wiring, LED states, hat design.

- MCU: STM32F407VET6, 168MHz. Board: STM32_F4VE V2.0
- SD: SDIO 1-bit mode, FatFS
- CAN1: PB8 (RX), PB9 (TX) — 500 kbit/s
- LEDs: PA6, PA7 (active-low). Button: PE3 (shutdown)
- E2E test source: `../cansult` (Nissan Consult → CAN, 3 messages @ 20Hz)
