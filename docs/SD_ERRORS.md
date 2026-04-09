# SD Write Errors — Investigation Notes

## Resolution (2026-04-10)

**Root cause found and fixed.** The original analysis below correctly
noted that SD cards stall for internal garbage collection, but it
misidentified *where* that stall was blowing up. After instrumenting
`sd_diskio.c` with per-failure-point counters, `SD_write` turned out to
be handling the GC stalls fine — the failures all came from FatFS
`validate()` via the `SD_status` path:

```
f_sync → validate(&fp->obj, &fs)
       → disk_status(pdrv)              (in ff.c)
       → SD_status(lun)                 (in sd_diskio.c)
       → BSP_SD_GetCardState()          (polls CMD13 once)
       → returns MSD_ERROR if card != TRANSFER
       → STA_NOINIT set
       → validate returns FR_INVALID_OBJECT (FR_9)
```

On a stress workload (64 U16 fields × 16 CAN IDs @ 250 Hz), the
SanDisk Ultra briefly re-enters PROGRAMMING for background GC *after*
a write completes. The next `f_sync` catches that window, `SD_status`
rejects it, and the file operation fails even though no block write
was ever attempted. This exactly matches the `FR_9@write` / `FR_9@sync`
errors in the timeline below — they were never about a corrupted FIL
object, they were validate() snapshots of a transient card state.

**Fix (commit `6d96480`):** wrap `SD_status` in a 100 ms retry loop
with `HAL_Delay(1)` between polls. The yield lets the card actually
exit PROGRAMMING instead of being hammered by CMD13; 100 ms is well
above the typical GC stall (a few ms) yet bounded enough to keep the
main loop responsive on real hard failures.

**Instrumentation (commit `aaa2d72`):** `sd_diskio_counters.h` exposes
per-failure-point counters for both `SD_write` (4 early-return paths)
and `SD_status` (calls / transient fails / retry rescues / hard
fails / max retry wait). Printed by `cmd_status` as the `sdw:` and
`sdst:` lines so regressions are visible from `make cdc-cmd CMD=status`.

**Verification:** 10+ min stress run on SanDisk Ultra 32GB —
`recovery_count = 0`, `files = 1` (no rotation from recovery),
`sdst rescued = 11/11 hard = 0`, max retry wait `8 ms` out of the
100 ms budget. Previous baseline without the fix hit `rec ≈ 24` in
~10 min and entered FAULT state.

The historical investigation notes below are preserved as context —
they describe the failures that led to the fix, not the final state.

---

## Observed problem (2026-04-06)

After continuous logging, `lw_tick` gets `FR_DISK_ERR` (FatFS code 1) from `f_write` or `f_sync`.
After 5 consecutive errors (`MAX_ERROR_COUNT`), firmware enters `error_state=1` and stops logging until reboot.

## Timeline from sessions

Old card (16GB), no buffering:
```
uptime=1199s err=5/1  last=FR_1@write — FATAL (~20 min, sync every 100 blocks)
```

SanDisk Ultra 32GB A1, no buffering:
```
uptime=3974s err=5/1  last=FR_1@write — FATAL (~66 min, sync every 100 blocks)
uptime=406s  err=5/1  last=FR_1@sync  — FATAL (~7 min, sync every 10 blocks)
```

SanDisk Ultra 32GB A1, with 4KB I/O buffer:
```
uptime=947s  err=5/1  last=FR_9@write — FATAL (~16 min, sync every 100 blocks)
uptime=108s  err=5/1  last=FR_9@write — FATAL (~2 min, with simple retry)
uptime=108s  err=5/1  last=FR_1@recovery files=3 — FATAL (~2 min, with remount recovery)
```

SanDisk Ultra 32GB A1, with 4KB I/O buffer + f_expand pre-allocation:
```
uptime=2070s err=5/1  last=FR_1@recovery rec=1 files=2 — FATAL (~34 min)
```

Key finding: **more frequent f_sync triggers failure faster**.
Buffering (4KB) extends time ~4x but doesn't eliminate the problem.
f_expand pre-allocation adds another ~70% (34 min vs 20 min) by eliminating FAT updates.
After FR_DISK_ERR, FIL object becomes invalid (FR_9/FR_INVALID_OBJECT) — retry on same file is useless.
Recovery with remount works 1-2 times but card eventually fails repeatedly.

## Root cause analysis

**SD card internal garbage collection (GC) stalls.** All SD cards periodically block I/O for up to 200ms during internal housekeeping (wear-leveling, GC). When f_write/f_sync hits this window, SDIO times out → FR_DISK_ERR.

Evidence:
- Two different cards, same symptom → not card-specific
- f_sync every 10 blocks fails in 7 min, every 100 blocks lasts 60+ min → frequency-dependent
- Error is always on I/O operation (write or sync), not on data corruption

## Conditions

- MCU: STM32F407VET6 Rev 2.0, SDIO 4-bit mode, ClockDiv=4
- FatFS polled mode (no DMA), 4KB I/O buffer, f_expand pre-allocation
- CAN: 2 active IDs (~50Hz), 13 fields
- Write rate: ~50 records/sec, ~46 bytes each (~2.3 KB/s)

## What we tried

### GPIO Pull-up on SDIO pins (FIXED different issue)
- Root cause: SanDisk Ultra A1 failed CMD8 handshake without pull-ups
- Fix: `GPIO_PULLUP` on PC8-PC12, PD2
- Result: card initializes correctly now

### SDIO Hardware Flow Control (BROKEN — errata)
- STM32F407 errata ES0182: HW flow control causes SDIOCLK glitches → data corruption
- All chip revisions affected, no workaround — must keep disabled

### Reduced f_sync interval (WORSE)
- sync every 10 blocks → failed in 7 min (was 60+ min with sync/100)
- More sync = more chance to hit GC window

### 4KB I/O buffer (HELPS ~4x)
- Accumulate records in RAM, write 4KB chunks instead of 46-byte writes
- Reduces SD transactions from 50/sec to ~0.6/sec
- Extended stable time from ~16 min to ~66 min, but doesn't eliminate problem

### Write retry on same file (USELESS)
- After FR_DISK_ERR, FIL object is invalid → all retries get FR_9
- HAL_Delay between retries doesn't help

### Recovery: close + new file (PARTIALLY WORKS)
- Close failed file, create new one, retry buffer
- Works 1-2 times, then card stops responding entirely

### Recovery: close + remount + new file (PARTIALLY WORKS)
- Full unmount → 1s delay → remount → new file
- Slightly better but card still fails after a few recoveries

### f_expand pre-allocation (HELPS ~70%)
- `f_expand(&file, 4MB, 1)` right after `f_open`, before any writes (objsize must be 0)
- `f_lseek(&file, 0)` after expand, then write header and data normally
- `f_truncate()` before `f_close()` to trim to actual size
- Use `f_tell()` instead of `f_size()` for rotation check (f_size = MAX_FILE_SIZE after expand)
- Previous attempt failed because f_expand was called after header write → FR_DENIED
- Extended stable time from ~20 min to ~34 min
- Still crashes eventually — GC stalls happen even without FAT updates

## rusEFI comparison (reference: ~/src/oss/rusefi)

Key files:
- `firmware/hw_layer/mmc_card.cpp` — SD driver, logging thread
- `firmware/controllers/system/buffered_writer.h` — 512B buffer with write-through
- `firmware/hw_layer/ports/stm32/stm32f4/cfg/mcuconf.h` — SDIO config

### What rusEFI does differently:
1. **f_expand(fd, 32MB, 1) + f_truncate on close** — pre-allocates contiguous space, eliminates FAT updates during writes. Truncates to actual size when done.
2. **4-bit SDIO mode** — we also use 4-bit (BSP_SD_Init reconfigures after 1-bit init).
3. **ChibiOS SDC driver with DMA** — write timeout 250ms, read timeout 25ms. We use HAL polled mode (next to fix).
4. **SDC_INIT_RETRY = 100** — 100 init retries at 10ms intervals.
5. **f_sync every 10 writes** (~2 sec at 20Hz) — similar to our sync/100 at 50Hz.
6. **No retry/recovery on write failure** — logger stops until reboot. Simpler than our approach.

### What's the same:
- FatFS, MLG format, no power-loss protection
- 512B BufferedWriter (we have 4KB — better)
- Error counting with threshold

## Implemented improvements

### 1. 4KB I/O buffer — DONE
Accumulate records in RAM, write in 4KB chunks. ~0.6 writes/sec instead of 50/sec.

### 2. Recovery with remount — DONE
On write failure: close file → unmount → 1s delay → remount → new file → retry buffer.

### 3. f_expand() pre-allocation — DONE
`f_expand(4MB, 1)` after open, before header. `f_truncate()` on close. Eliminates FAT updates.

### 4. 4-bit SDIO — ALREADY ACTIVE
BSP_SD_Init calls `HAL_SD_ConfigWideBusOperation(&hsd, SDIO_BUS_WIDE_4B)`.

### 5. SDIO timeout — ALREADY OK
SD_TIMEOUT = 30 seconds (sd_diskio.c default). Sufficient for GC stalls.

### 6. DMA for SDIO — DONE
`BSP_SD_WriteBlocks_DMA` / `ReadBlocks_DMA` with DMA2 Stream 6 (write) /
Stream 3 (read). See `sd_write_dma.c` for the BSP override that issues
`HAL_DMA_Start_IT` before `SDMMC_CmdWriteBlock` (the stock HAL order
caused TX_UNDERRUN under load). CPU no longer spin-waits on the data
path; only the CMD13 polling loops in `SD_CheckStatusWithTimeout` and
`SD_status` remain synchronous.

### 7. SD_status retry loop — DONE (commit `6d96480`)
100 ms retry in `SD_status` with `HAL_Delay(1)` between CMD13 polls,
so FatFS `validate()` survives transient PROGRAMMING state from
background GC. Fully eliminates the recovery loop on stress workloads.
See the "Resolution" section at the top of this file for root cause.

## Remaining improvements

(none at this time — the stress workload is stable)

## References

- [STM32F407 errata ES0182](https://www.st.com/resource/en/errata_sheet/es0182-stm32f405407xx-and-stm32f415417xx-device-errata-stmicroelectronics.pdf) — HW flow control bug
- [SD card GC stalls](https://www.embedded.com/taking-out-the-garbage/)
- [FatFS f_expand](https://elm-chan.org/fsw/ff/doc/expand.html)
- [FatFS application notes](https://elm-chan.org/fsw/ff/doc/appnote.html)
- [STM32F4 SDIO best practices](https://blog.frankvh.com/2011/12/30/stm32f2xx-stm32f4xx-sdio-interface-part-2/)
- [Betaflight Blackbox approach](https://betaflight.com/docs/development/Blackbox)
- [rusEFI mmc_card.cpp](https://github.com/rusefi/rusefi/blob/master/firmware/hw_layer/mmc_card.cpp)
