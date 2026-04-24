# AGENTS.md

Universal instructions for AI coding agents working in this repository
(Claude Code, Cursor, Codex CLI, Aider, etc.).

Universal CAN bus data logger. STM32F407VET6, FreeRTOS, SD/FatFS, MLVLG v2 logs.

Docs: `docs/REQUIREMENTS.md` · `docs/ARCHITECTURE.md` · `docs/HARDWARE.md` · `docs/CONFIG_GUIDE.md` · `docs/DEBUG.md`

## Layout

- `firmware/Lib/` — pure C business logic, host-testable (Unity). No HAL, no CubeMX markers.
- `firmware/Src/` — HAL glue. CubeMX `USER CODE BEGIN/END` markers mandatory; custom code must stay within them.
- `firmware/test/` — host unit tests.
- `firmware/mlg-test/` — Node.js MLG parser (integration validation).

Build system: STM32CubeIDE headless (`.project`/`.cproject`).
TDD: business logic lives in `Lib/`, tested on host with native gcc + Unity.

## Commands (from `firmware/`)

```
make build | clean | test | flash | erase | reset
make ocd-server | ocd-debug | ocd-status        # OpenOCD
make gdb-server | debug                          # ST-LINK GDB server
make gdb-read EXPRS="var1 var2"                  # one-shot variable read
make gdb-exec SCRIPT=file.gdb
make cdc-cmd CMD=status                          # USB CDC CLI
make cdc-put FILE=config.ini                     # upload file to SD via CDC
```

## Conventions

- **FreeRTOS** — CMSIS_V2, heap_4 (16 KB). Tasks: `task_producer` (CAN + debug, osPriorityNormal, 2 KB) and `task_sd` (SD writer, osPriorityBelowNormal, 4 KB). Shadow `field_values` is guarded by `shadow_mutex`. HAL timebase on TIM6 (SysTick belongs to FreeRTOS). In `Src/` use `osDelay`, not `HAL_Delay`.
- **RAM** — `config` (~53 KB) placed in CCM via `.ccmram`; `ring_Buffer` (64 KB) in main SRAM. The MLG header is built on the fly from `cfg_Config`.
- **MLVLG v2** — big-endian. `DisplayValue = (rawValue + transform) * scale`.
- **Config** — INI file on SD. Extended 29-bit via `is_extended = 1`; sub-byte fields via `start_bit` + `bit_length`; plausibility via `valid_min`/`valid_max` + `invalid_strategy` (last_good/clamp/skip) and `preset` (aem_uego). See `docs/CONFIG_GUIDE.md`.
- **GPS** — optional NMEA receiver (u-blox NEO-6M and compatibles) on USART3 @ 9600 8N1 (PB10 TX / PB11 RX) via DMA1_Stream1 circular RX. Parser `Lib/gps_nmea.c` (host-testable, GGA/RMC); driver `Src/gps_drv.c` (NDTR polling from `task_producer`, buffer in main SRAM — DMA1 cannot see CCM). `[gps] enable = 1` auto-injects the minimal `gps_lat/lon/alt/speed_kmh/fix` set; extend via `[field] source = gps:*`. One-shot RTC sync on the first fix that carries a date. CDC: `gps` (snapshot), `gps_raw` (toggle NMEA passthrough).
- **RTC** — LSE + CR1220 on VBAT; survives resets via `RTC_FLAG_INITS`. Set through the CDC command `settime`. Files named `YYYY-MM-DD_HH-MM-SS_NN.mlg` (LFN); collisions handled via `FA_CREATE_NEW` + `_NN`.
- **FatFS** — `_USE_LFN = 2`, `_FS_REENTRANT = 0` (single-task, accessed only from `task_sd`).
- **BKP registers** — `Src/bkp_log.c`: DR1 session, DR2 fault session, DR3 packed fault. CDC `lastfault`.
- **Option Bytes** — BOR Level 3 (2.70 V); the MCU is held in reset until Vdd is stable (keeps the SD card from hitting a bad init on a slow power-up ramp).

## Hardware (summary; details in `docs/HARDWARE.md`)

- MCU STM32F407VET6 @ 168 MHz on the STM32_F4VE V2.0 board.
- CAN1: PB8/PB9 (RX/TX), default 500 kbit/s; TJA1050 transceiver on a modified MCP2515 board (SO→TX, SI→RX).
- SD: SDIO 4-bit + FatFS.
- USB CDC on PA11/PA12.
- GPS (optional): USART3 PB10/PB11 → NEO-6M 4-pin (VCC 5V / GND / TX → PB11 / RX ← PB10), active antenna via U.FL/MHF1 → SMA.
- LEDs: PA6/D2, PA7/D3 (active-low). Buttons: PE3/K1 (shutdown), PE4/K0 (MLG marker).
