# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

CAN bus data logger firmware for STM32F407VET6 microcontroller. Logs data to SD card in MLVLG binary format (compatible with MegaLogViewer). The project is in active early development (iterative prototyping stage).

## Repository Structure

- `firmware/Lib/` — Pure business logic (no HAL dependencies), testable on host
- `firmware/Src/` — HAL-dependent firmware code (main, log_writer, peripherals)
- `firmware/Inc/` — Headers for Src/
- `firmware/test/` — Host-based unit tests (Unity framework)
- `firmware/mlg-test/` — Node.js test harness for `.mlg` file validation
- `docs/` — MLVLG format specs (PDFs), board schematic, rusefi reference implementation

## Commands (from `firmware/`)

```bash
make build       # RTC time gen + CubeIDE headless build → Debug/canlogger.elf
make clean       # Clean rebuild
make test        # Run host unit tests (Unity)
make flash       # Build + flash via ST-Link SWD
make erase       # Full chip erase
make reset       # Hardware reset
make gdb-server  # Start ST-LINK GDB server on :3333
make debug       # Build + GDB connect + load
```

Build system is STM32CubeIDE (`.project`/`.cproject`). There is also a legacy `CMakeLists.txt` for CLion.

## Testing

TDD approach. Pure logic lives in `firmware/Lib/`, tested on host with native gcc + Unity framework.

```bash
cd firmware/test && make    # or: cd firmware && make test
```

- Tests glob all `../Lib/*.c` automatically
- Test files follow `test_*.c` naming convention
- Unity framework lives in `firmware/test/unity/`

## Architecture

### Code Organization

- **`Lib/`** — HAL-free business logic. Currently: MLVLG v2 binary format encoder (`mlvlg.c/h`). Compiled both for target (via CubeIDE) and host (via test Makefile).
- **`Src/main.c`** — HAL init, peripheral setup, main loop. Button press (EXTI on PE3) triggers graceful shutdown.
- **`Src/log_writer.c`** — SD card logging: FatFS mount, file creation (8.3 names from RTC), file rotation at 4MB, LED status, error retry.

### Key Hardware

- MCU: STM32F407VET6, 168MHz (HSE + PLL)
- SD card: SDIO 1-bit mode via FatFS
- LEDs: PA6 (LED1), PA7 (LED2), active-low
- Button: PE3 (K1), EXTI rising edge — shutdown
- RTC: LSI oscillator, BCD format
- Board schematic: `docs/STM32F407VET6-STM32_F4VE_V2.0_schematic.pdf`

### LED Behavior

- LED1 solid = SD mounted and logging
- LED2 blinking (500ms) = normal operation
- Both LEDs fast blink (100ms) = error state
- LED1 off + LED2 solid = stopped

### MLVLG Format

Official spec PDFs, board schematic, and rusefi reference implementation are in `docs/`. See `docs/README.md` for index.

We target MLVLG v2. All data is big-endian. Key structure:
- Header (24 bytes): magic "MLVLG\0", version=2, unix timestamp, info data start, data begin index, record length, field count
- Logger Field[] (89 bytes each): type, name(34), units(10), display style, scale(F32), transform(F32), digits, category(34)
- Data blocks: block type(1) + counter(1) + timestamp_10us(2) + field data + CRC(1 byte sum)
- Markers: block type=1 + counter(1) + timestamp(2) + message(50)

Display formula: `DisplayValue = (rawValue + transform) * scale`

### Code Style

STM32CubeMX conventions with `USER CODE BEGIN/END` markers. Custom code in `Src/` must stay within these markers to survive CubeMX re-generation. Code in `Lib/` is free from this constraint.
