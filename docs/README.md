# Documentation

## Project overview

- [REQUIREMENTS.md](REQUIREMENTS.md) — vision, roadmap, task status
- [ARCHITECTURE.md](ARCHITECTURE.md) — modules, FreeRTOS tasks, data flows, config format, test strategy

## User guides

- [CONFIG_GUIDE.md](CONFIG_GUIDE.md) — how to write `config.ini`: fields, types, scale/offset, LUT, plausibility filter, presets, MLG files, troubleshooting
- [DEBUG.md](DEBUG.md) — USB CDC CLI (`status`, `stream`, `config`, `ls`, `get`, `mark`, `settime`, `lastfault`), `usb_get.py`, uploading configs via `cdc-put`

## Hardware

- [HARDWARE.md](HARDWARE.md) — STM32_F4VE V2.0 board, pins, peripherals, SWD, hat, E2E bench
- [HAT_PCB.md](HAT_PCB.md) — production spec for the hat PCB: BOM, pin allocation, schematic blocks, layout hints, TODO
- [HAT_PROTOTYPE.md](HAT_PROTOTYPE.md) — breadboard hat prototype: DC-DC, supercap, VIN_SENSE divider
- [PPK_MEASUREMENTS.md](PPK_MEASUREMENTS.md) — PPK2 current measurements for supercap sizing (graceful shutdown)

## Postmortem — completed investigations

- [postmortem/SD_ERRORS.md](postmortem/SD_ERRORS.md) — FR_DISK_ERR during long writes; root cause via `SD_status`/`validate()`; fix 2026-04-10
- [postmortem/SD_WRITER_DECOUPLING.md](postmortem/SD_WRITER_DECOUPLING.md) — motivation and plan for migrating to FreeRTOS (GC stalls up to 710 ms blocked the main loop)
- [postmortem/STRESS_TEST_128U16_PLAN.md](postmortem/STRESS_TEST_128U16_PLAN.md) — final max-stress run 128 U16 @ 1 kHz, 2 h 27 min, no errors
- [postmortem/CMD_RSP_TIMEOUT.md](postmortem/CMD_RSP_TIMEOUT.md) — nature of CTIMEOUT during DMA writes; CMD12/CMD13 against a busy card

## External references

- [reference/](reference/) — MLVLG specification (PDF), board schematic, rusEFI reference. See [reference/README.md](reference/README.md)
