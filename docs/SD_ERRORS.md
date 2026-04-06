# SD Write Errors — Investigation Notes

## Observed problem (2026-04-06)

After ~20 minutes of continuous logging, `lw_tick` gets `FR_DISK_ERR` (FatFS code 1) from `f_write`.
After 5 consecutive errors (`MAX_ERROR_COUNT`), firmware enters `error_state=1` and stops logging until reboot.

## Timeline from session

```
uptime=132s  err=0/0  size=62039   blocks=86   — OK
uptime=902s  err=0/0  size=416078  blocks=119  — OK
uptime=975s  err=0/0  size=449658  blocks=43   — OK
uptime=1199s err=5/1  size=0       blocks=231  last=FR_1@write — FATAL
```

Failure happened between 975s and 1199s (~16-20 minutes).

## Conditions

- SD: 16GB, SDIO 1-bit mode
- CAN: 2 active IDs (0x640 @ ~50Hz, 0x665 @ ~1Hz), 13 fields
- USB CDC active (status polling every 3 min)
- f_sync every 100 blocks
- f_sync result was not checked (ignored)

## Diagnosis added

Added `last_error` (FRESULT code) and `last_error_at` (source tag) to `lw_Status`.
Tags: `mount`, `cfg_open`, `cfg_read`, `cfg_parse`, `can_init`, `create`, `header`, `write`, `rotate`, `sync`.

Status now shows: `err=5/1 last=FR_1@write`

## Possible causes

1. **SD card GC/wear-leveling latency** — cheap cards can stall for 100-500ms during internal housekeeping, causing SDIO timeout
2. **SDIO timing drift** — MCU temperature rise after prolonged operation could affect clock margins
3. **Power supply sag** — simultaneous CAN RX + SD write + USB could cause voltage dip
4. **SD card quality** — low-endurance card, not designed for continuous writes

## Planned fixes (see REQUIREMENTS.md v1.0)

1. Check f_sync result (currently ignored)
2. Recovery instead of fatal: close -> remount -> new file -> continue
3. Reduce sync interval (100 -> 10 blocks)
4. Cooldown after recovery (1s pause)
5. Recovery counter in status output
6. Investigate: SDIO clock divider, SD power, different card
