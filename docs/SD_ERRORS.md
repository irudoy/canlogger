# SD Write Errors — Investigation Notes

## Observed problem (2026-04-06)

After continuous logging, `lw_tick` gets `FR_DISK_ERR` (FatFS code 1) from `f_write` or `f_sync`.
After 5 consecutive errors (`MAX_ERROR_COUNT`), firmware enters `error_state=1` and stops logging until reboot.

## Timeline from sessions

Old card (16GB):
```
uptime=132s  err=0/0  size=62039   blocks=86   — OK
uptime=902s  err=0/0  size=416078  blocks=119  — OK
uptime=1199s err=5/1  last=FR_1@write — FATAL (~20 min, sync every 100 blocks)
```

SanDisk Ultra 32GB A1:
```
uptime=3974s err=5/1  last=FR_1@write — FATAL (~66 min, sync every 100 blocks)
uptime=406s  err=5/1  last=FR_1@sync  — FATAL (~7 min, sync every 10 blocks)
```

Key finding: **more frequent f_sync triggers failure faster**.

## Root cause analysis

**SD card internal garbage collection (GC) stalls.** All SD cards periodically block I/O for up to 200ms during internal housekeeping (wear-leveling, GC). When f_sync hits this window, SDIO times out → FR_DISK_ERR.

Evidence:
- Two different cards, same symptom → not card-specific
- f_sync every 10 blocks fails in 7 min, every 100 blocks lasts 60+ min → frequency-dependent
- Error is always on I/O operation (write or sync), not on data corruption

## Conditions

- MCU: STM32F407, SDIO 1-bit mode, ClockDiv=4 (8 MHz)
- FatFS, no DMA (polled mode)
- CAN: 2 active IDs (~50Hz), 13 fields
- Write rate: ~50 records/sec, ~46 bytes each (~2.3 KB/s)
- f_write called per record (small writes, not buffered)

## Planned fixes (priority order)

### 1. Pre-allocate file with f_expand()
Pre-allocate contiguous space (e.g. 4 MB = MAX_FILE_SIZE) when creating log file.
Eliminates FAT table updates during writes — major source of GC triggers.

### 2. Buffer writes to cluster-aligned blocks
Instead of f_write per record (~46 bytes), accumulate in RAM buffer and write 4KB+ chunks.
Reduces number of SD transactions, avoids hitting GC window on small writes.

### 3. Reduce f_sync frequency
Sync every 100-500 blocks (5-25 seconds at 50ms interval). Less sync = less chance of hitting GC.
Max data loss at crash = last sync interval.

### 4. Measure sync duration
Profile f_sync time. If >200ms, confirms GC stall hypothesis. Add to status output.

### 5. SDIO hardware flow control
Enable `SDIO_HARDWARE_FLOW_CONTROL_ENABLE` — SDIO controller pauses when card signals busy,
instead of timing out.

### 6. Recovery instead of fatal
On FR_DISK_ERR: close → remount → new file → continue. Cooldown 1s.
With f_expand + buffered writes, data loss limited to current buffer.

### 7. DMA for SDIO
Move from polled to DMA mode. Reduces CPU involvement, more tolerant of card stalls.
Requires 32-bit aligned buffers.

## References

- [SD card GC stalls](https://www.embedded.com/taking-out-the-garbage/)
- [FatFS f_expand](https://elm-chan.org/fsw/ff/doc/expand.html)
- [FatFS application notes](https://elm-chan.org/fsw/ff/doc/appnote.html)
- [STM32F4 SDIO best practices](https://blog.frankvh.com/2011/12/30/stm32f2xx-stm32f4xx-sdio-interface-part-2/)
- [Betaflight Blackbox approach](https://betaflight.com/docs/development/Blackbox)
