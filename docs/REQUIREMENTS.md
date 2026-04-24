# Requirements and roadmap: CAN Logger

## Project vision

A standalone universal logger for CAN-bus parameters that writes to an SD card in MLVLG v2 format (compatible with MegaLogViewer).

**Key property — universality.** The logger does not know in advance which CAN messages and parameters it will record. The CAN → MLG mapping is described by a configuration file on the SD card. That lets one device be used across different vehicles and protocols without reflashing.

## Hardware architecture

See [HARDWARE.md](HARDWARE.md) — board, pins, wiring, hat, E2E bench.

## Software architecture

See [ARCHITECTURE.md](ARCHITECTURE.md) — modules, data flows, config format, test strategy.

## Roadmap

### PoC — done

**Goal:** prove the concept — produce a valid MLVLG file from test data and open it in MegaLogViewer.

Approach: TDD — host tests first, then on-device verification.

Outcome:
- [x] Clean up legacy code (drop `Src/mlvlg.c`, `Inc/mlvlg.h` — replaced by `Lib/mlvlg.*`)
- [x] Snapshot test: generate a full .mlg file on the host and validate it through `mlg-converter`
- [x] Integrate `Lib/mlvlg` with SD writes: header + fixed fields + test data blocks
- [x] Write test data (counter / sine wave) to an MLVLG file on the device
- [x] Open the file in MegaLogViewer and verify correctness (parameters visible, plots work)

21 tests (unit + snapshot), 0 failures. 453 records at 20 Hz, validated against mlg-converter and MegaLogViewer.

### MVP — basic working logger — done

**Goal:** capture CAN data from cansult to SD as a valid MLVLG file openable in MegaLogViewer.

Outcome:
- [x] Enable CAN peripheral in CubeMX (CAN1 PB8/PB9, 500 kbit/s)
- [x] Implement `can_drv` — interrupt-driven CAN RX → ring buffer (`Lib/ring_buf`)
- [x] Implement `Lib/config` — INI config parser from SD
- [x] Implement `Lib/can_map` — CAN frame → MLG field-value mapping driven by config
- [x] Config-driven MLG header + data block writer to SD (in `log_writer`)
- [x] Main-loop integration: config → can_map → mlvlg → sd_write
- [x] Behaviour without a config: error + LED indication + K1 shutdown
- [x] Hand-written cansult config (11 parameters) — `test/cansult_config.ini`
- [x] Tests: config parser, mapping, bit extraction, ring buffer (58 tests)
- [x] E2E test: cansult → Nissan ECU → CAN → canlogger → SD → MegaLogViewer
- [x] MegaLogViewer check: Battery-voltage graph 11.4 V–15.5 V with real-world swings

Configurable CAN bitrate, hardware ID filters, Unix timestamp in MLG header.

### Next stage — debugging and stability

- [x] Debug data exchange without pulling the SD → **USB CDC** (Virtual COM Port)
- [x] Investigate and fix the cansult UART↔ECU dropout after some runtime
- [x] User-facing documentation on configuration and MLG format
- [x] Debug interface improvements: USB CDC CLI (help/status/stream/config/ls/get), MLG file download (usb_get.py)

### v1.0 — stable product

**Goal:** dependable logger for day-to-day use.

#### Hardware

- [ ] Hat PCB: CAN transceiver + DC-DC + shutdown circuit (breadboard prototype, see [HAT_PROTOTYPE.md](HAT_PROTOTYPE.md))
- [ ] RC debounce for the marker/shutdown buttons on the hat (100 nF + optional 1–10 kΩ near the PE4/PE3 connector) — removes the 300 ms software lockout and kills contact-bounce on release. Do not populate the debug K0/K1 buttons on the main board in production
- [ ] Active buzzer on the hat — audible status check without looking at the board: mount OK, config/SD errors, fault-on-boot (from BKP). Complements the LEDs and is audible from under the dashboard
- [~] GPS module — geolocation + accurate wall-clock time. Firmware: NMEA parser GGA/RMC + USART3 DMA circular RX; config `[gps] enable = 1` auto-injects `gps_lat/lon/alt/speed_kmh/fix`; optional fields via `source = gps:<tag>`; one-shot RTC sync on the first fix that carries a date; CDC `gps`/`gps_raw`. Hardware: needs an active antenna with a proper RF path (indoors through low-E glass the NEO-6M sees at most three satellites and never locks — ordered a Triada 2178 + U.FL→SMA pigtail)

#### Software

- [x] Graceful shutdown on power loss — VIN_SENSE ADC + armed debounce + lw_stop + auto-resume via NVIC_SystemReset; validated on a supercap breadboard
- [x] Extended CAN ID (29-bit) support in the config and can_map — explicit `is_extended = 1` key in `[field]`; the hardware filter is set up correctly for IDE=1 (used by AEM 30-0300 `0x180`)
- [x] Sub-byte (1–7 bit) fields in the config — `start_bit` + `bit_length` keys for individual bit flags (ECU status, relays, solenoids)
- [x] Field plausibility filter in the config — `valid_min` / `valid_max` keys (in display units, after scale/offset/LUT) + `invalid_strategy = last_good | clamp | skip`. Presets for sensor-fault encodings (`preset = aem_uego` → rejects raw 0xFFFF on AFR/Lambda). Covers RPM spikes from cansult UART glitches and AFR=96 on decel fuel-cut
- [ ] Circular logging — when the SD fills up, delete the oldest MLG files and keep writing
- [ ] Config validation on load with diagnostics
- [x] "Date" field in MLG (U32 Unix timestamp, display_style=MLG_DATE) — synthetic field placed first in every record, +4 bytes/snapshot
- [x] MLG markers (native block type 0x01) — K0 button (PE4, EXTI falling, 300 ms any-edge debounce, msg=`btn`) + CDC `mark [txt]`. D2 briefly goes dark (~75 ms) on marker write. Helps locate events quickly in MegaLogViewer
- [ ] Expose `max_file_size` via config.ini (currently hard-coded 512 MB)
- [ ] Debug log on SD — system events, errors, conditional data samples
- [ ] Statistics logging (frames received/lost/written)
- [ ] Hardware-level CAN ID filtering (HAL CAN filter banks)
- [ ] Dual CAN bus support (CAN1 + CAN2) — two independent channels, doubled throughput
- [ ] Consider migrating the debug interface from USB CDC to CAN (status/config/get over CAN frames, drop the USB dependency)
- [ ] MLG completeness — study the spec, check whether all features are used
- [x] RTC from LSE (32.768 kHz) instead of LSI — accurate log timestamps, VBAT battery on the board
- [x] RTC persistence: `RTC_FLAG_INITS` check — the RTC is not overwritten on boot if already initialised; survives reset/flash as long as VBAT is alive. First boot (fresh chip / VBAT loss) → bootstrap to `2026-01-01 00:00:00`, then set via CDC `settime YYYY-MM-DD HH:MM:SS` or GPS
- [x] Persistent fault log in RTC backup registers (DR1–DR3): session counter + last fault code/location; survive reset and power loss. CDC `lastfault` command. Needed when the SD does not mount — the BKP fault is visible on the next boot
- [x] BOR Level 3 (2.7 V) in option bytes — MCU held in reset during slow power ramps; no SD init attempts on an unstable Vdd
- [x] SD mount retry — up to 5 attempts with 200 ms pauses in `lw_init`; the card has time to initialise after a cold start
- [x] FatFS LFN (Long File Names) + new naming scheme: `2026-04-12_12-32-29_00.mlg` — chronological sort; on collision the `_NN` suffix increments, `FA_CREATE_NEW` (does not overwrite existing files)
- [x] Renamed SD config `cansult_config.ini` → `config.ini`
- [x] AEM X-Series UEGO 30-0300 (AEMnet CAN): Lambda, AFR (computed scale 0.001465), O2%, SysVolts on extended ID `0x180`
- [x] State indication via LEDs (recording, error, no config, no SD)
- [x] DMA for SDIO (reorder + PBURST_INC4, TX_UNDERRUN fixed)
- [x] SD error handling (FR_DISK_ERR@write after ~20 min, see [postmortem/SD_ERRORS.md](postmortem/SD_ERRORS.md), [postmortem/CMD_RSP_TIMEOUT.md](postmortem/CMD_RSP_TIMEOUT.md)):
  - [x] Check the f_sync result — recover on error
  - [x] Recovery instead of fatal: close → remount → new file → keep writing
  - [x] `recover_file` does `f_truncate` + `f_sync` before `f_close` — without this, the SD was left with 32 MB files containing garbage tails from reused clusters (visible in log-19-04: two files exactly MAX_FILE_SIZE with a break after the first recovery). Truncate/sync are gated by `had_clean_sync`: they run only if at least one periodic `f_sync` succeeded in the current file. Otherwise the FAT state after a fresh error is suspect, further SDIO ops may hang — we skip them, the old file stays 32 MB with a garbage tail, the new file is created clean
  - [~] ~~Shrink the f_sync interval (100 → 10 blocks)~~ — not needed: supercap + graceful shutdown guarantee a flush at power-down, and more frequent f_sync increases the chance of a GC stall
  - [x] Replace HAL_Delay(1000) in recover_file() with something non-blocking — osDelay (FreeRTOS, blocks only task_sd)
  - [x] Recovery counter in status (rec=N lastrec=FR_X@site)
  - [x] Drop the duplicate mlg_fields[256] (23 KB RAM) — build the MLG header on the fly from cfg_Config
  - [x] recover_file() used to block the main loop for 30 s — now only task_sd blocks (osDelay), CAN drain keeps going
  - [x] Stress test at max pressure: 128 fields / 32 CAN IDs / 1 ms — solved by migrating to FreeRTOS (GC stalls block only task_sd)
- [x] Investigate CMD_RSP_TIMEOUT on DMA writes — see [postmortem/CMD_RSP_TIMEOUT.md](postmortem/CMD_RSP_TIMEOUT.md)
  - [x] Analysis: most likely CMD12 stop against a busy card, see docs/postmortem/CMD_RSP_TIMEOUT.md
  - [x] SDIO error counters via HAL_SD_ErrorCallback + hal.ErrorCode in CDC status
  - [x] FAULT file on SD on a fatal error (FAULT_NN.TXT with full diagnostics)
- [x] **SD writer decoupling — FreeRTOS migration**
      (see [postmortem/SD_WRITER_DECOUPLING.md](postmortem/SD_WRITER_DECOUPLING.md)):
  - root cause: blocking `SD_write` in the main loop → GC stalls up to 710 ms
    stop the whole pipeline → **~12 % sample loss** on a 2-hour run
    (64 U16 @ 250 Hz: expected 4000 fps, actual 3509 fps)
  - at the max-load 128 U16 @ 1 kHz the loss grows proportionally
  - reference: rusEFI `mmc_card.cpp` — dedicated SD thread at priority
    `NORMALPRIO-1`, `SDC_NICE_WAITING=TRUE`; main loop does not block
  - plan: CubeMX → Middleware → FREERTOS CMSIS_V2 → split out
    `task_sd` (osPriorityBelowNormal) with an MLG-records queue;
    `task_log` (osPriorityNormal) builds the snapshot → queue;
    CAN RX + can_map in a high-priority task
  - FatFS must be reentrant (`FF_FS_REENTRANT=1`)
  - `SD_status` retry: `HAL_Delay` → `osDelay` (yield-friendly)
  - final check: `docs/postmortem/STRESS_TEST_128U16_PLAN.md`
- [x] `RING_BUF_SIZE` 1024 → 4096 (64 KB, covers 225 ms @ 18k fps):
  - unblocked by the RAM optimisation (config → CCM, mlg_fields removed)
  - comfort target: 8192 (128 KB, 450 ms) — feasible once `can_rx_buf` moves to CCM (does not fit today)
- [x] RAM optimisation (main SRAM: 54 KB of 128 KB used, CCM: 53 KB of 64 KB):
  - Removed `mlg_fields[256]` (23 KB) — MLG header is built on the fly from `cfg_Config`
  - `config` (52.7 KB) moved to CCM SRAM (`.ccmram` section)
  - FreeRTOS heap 8 → 16 KB, sdTask stack 2 → 4 KB
  - `CAN_SNIFF_MAX` 16 → 32 (full coverage of CAN IDs in status)

### v2.0 — configuration convenience

**Goal:** simple config authoring without manual editing.

Tasks:
- [ ] Web UI for generating the config file (pick CAN IDs, tune parameters)
- [ ] Sniffer mode: record raw CAN traffic for analysis and mapping authoring
- [ ] USB interface for downloading logs (`get`) and uploading configs (`put`) without pulling the SD (partially implemented for debugging, no user-facing UI yet)
- [ ] ESP Wi-Fi module

## Constraints and assumptions

- Target recording rate — as high as the current hardware can sustain
- STM32F407 has two CAN controllers (CAN1, CAN2) — both can be supported
- The config format may stay simple at first (INI/CSV) and be optimised later
- The logger does not transmit data — it only writes to SD
