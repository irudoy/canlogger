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

- **FreeRTOS** — CMSIS_V2, heap_4 (16 KB). Tasks: `task_producer` (CAN+debug, osPriorityNormal, 2 KB) и `task_sd` (SD writer, osPriorityBelowNormal, 4 KB). Shadow `field_values` под `shadow_mutex`. HAL timebase на TIM6 (SysTick принадлежит FreeRTOS). В `Src/` использовать `osDelay`, не `HAL_Delay`.
- **RAM** — `config` (~53 KB) в CCM через `.ccmram`; `ring_Buffer` (64 KB) в main SRAM. MLG-заголовок строится на лету из `cfg_Config`.
- **MLVLG v2** — big-endian. `DisplayValue = (rawValue + transform) * scale`.
- **Config** — INI на SD. Extended 29-bit через `is_extended = 1`; sub-byte через `start_bit` + `bit_length`; plausibility через `valid_min`/`valid_max` + `invalid_strategy` (last_good/clamp/skip) и `preset` (aem_uego). См. `docs/CONFIG_GUIDE.md`.
- **RTC** — LSE + CR1220 на VBAT; выживает через `RTC_FLAG_INITS`. Выставляется CDC-командой `settime`. Файлы `YYYY-MM-DD_HH-MM-SS_NN.mlg` (LFN), коллизии через `FA_CREATE_NEW` + `_NN`.
- **FatFS** — `_USE_LFN = 2`, `_FS_REENTRANT = 0` (single-task, только из `task_sd`).
- **BKP registers** — `Src/bkp_log.c`: DR1 session, DR2 fault session, DR3 packed fault. CDC `lastfault`.
- **Option Bytes** — BOR Level 3 (2.70 V), MCU держится в reset пока Vdd не стабилен (чтобы SD не ловила bad init на медленном нарастании питания).

## Hardware (summary; details in `docs/HARDWARE.md`)

- MCU STM32F407VET6 @ 168 MHz, плата STM32_F4VE V2.0.
- CAN1: PB8/PB9 (RX/TX), default 500 kbit/s; трансивер TJA1050 на модифицированной MCP2515-плате (SO→TX, SI→RX).
- SD: SDIO 4-bit + FatFS.
- USB CDC на PA11/PA12.
- LEDs: PA6/D2, PA7/D3 (active-low). Кнопки: PE3/K1 (shutdown), PE4/K0 (MLG marker).
