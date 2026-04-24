# SD Writer Decoupling — main loop blocks on GC stalls

## Symptom

On a 2 h 21 min run of `demo_stress_64u16.ini` (64 U16 × 16 CAN IDs × 250 Hz) the following numbers were recorded:

```
uptime=8478s frames=29748192 fields=64 init=1 err=0/0
sdw: tot=195401 lat=2/710 scratch=42044
sdst: calls=103401 fail=80 rescued=80 hard=0 maxret=8ms
```

At the expected generation rate of **4000 frames/s** (16 CAN IDs × 250 Hz) the actual rate was **3510 fps** (29.7 M / 8478 s). A shortfall of **~12 %**, equivalent to **~17 minutes of missed data on a 2-hour track**.

Reason: `err=0/0` and `rec=0` show that the pipeline is not crashing, but `sdw max_lat = 710 ms` means one of the GC stalls lasted almost a second, and **during that stall the main loop is fully stopped inside `SD_write`**. For 710 ms neither `demo_pack_can_frames`, nor `can_map_process`, nor `lw_tick` runs → the MLG file gets a **710 ms gap (~177 samples at 250 Hz)**. It is not an IO error, but **honest data loss** from the consumer's point of view (MegaLogViewer will see a flat line or a jump).

At the max load 128 U16 × 1 kHz the loss scales proportionally — the recording becomes unfit for production.

## Root cause: bare-metal single-loop architecture

The whole pipeline is synchronous, inside a single `while (1)`:

```
main loop:
  ├─ demo_pack_can_frames  (or CAN ISR drain via ring buffer)
  ├─ can_map_process
  ├─ lw_tick → f_write → SD_write → DMA start → wait DMA IRQ
  │                                            └─ poll CMD13 until TRANSFER
  │                                                  └─ if card is in PROGRAMMING:
  │                                                      loop up to 710 ms
  └─ debug_cmd_poll
```

`SD_write` is blocking. Until the card returns to the TRAN state, nothing else runs. This is a fundamental limitation of the single-loop design — it can only be softened with a bigger buffer and/or an architectural rework.

## Comparison with rusEFI (reference)

rusEFI solves the exact same problem on STM32F4 + ChibiOS. Key techniques (see `~/src/oss/rusefi/firmware/hw_layer/mmc_card.cpp:908-964`):

1. **Dedicated SD thread `MMCmonThread`** with priority
   `PRIO_MMC = NORMALPRIO-1` — **below everything else**
   (`thread_priority.h:43`). Stack size `3 × UTILITY_THREAD_STACK_SIZE`.
   Main loop (`NORMALPRIO+10`), CAN RX (`NORMALPRIO+6`), ADC are all
   higher priority. During an SD stall only the SD thread blocks;
   data keeps being collected.

2. **Shared `outputChannels` struct** instead of a queue. The main loop
   continuously updates fields. When woken, the SD thread snapshots the
   current state and writes a single MLG record. Intermediate values are
   "lost" as a timing gap, but every recorded point is fresh. No overflow,
   no drops — just a different sample rate.

3. **`SDC_NICE_WAITING = TRUE`** in `rusefi_halconf.h:46`. In ChibiOS
   `sdc_lld`, while polling the PROGRAMMING state it calls
   `osalThreadSleepMilliseconds(1)` instead of busy-wait — yields to the
   scheduler every millisecond. Other threads run even *inside* a GC
   stall.

4. **`f_expand(fd, 32 MB, 1)`** pre-allocation at file open time
   (`mmc_card.cpp:395-401`) — avoids FAT updates and the worst GC stalls.
   **We already do this (4 MB).**

5. **Single 512 B buffer** (`BufferedWriter<512>` in
   `buffered_writer.h:12`) — simple, because the thread architecture
   already solves the issue. No ping-pong, no big io_buf.

6. **`f_sync` every 10 f_writes** (`F_SYNC_FREQUENCY = 10`,
   `mmc_card.cpp:30`). At 4 writes/s that is one f_sync every ~2.5 s.

**Main takeaway**: the working architecture is **"block only the SD thread,
not the whole pipeline"**. Everything else (buffer sizes, sync frequency,
pre-allocation) is polish on top of that.

## Solution options

### A. Larger synchronous I/O buffer (4 KB → 256+ KB)

The main loop writes into `io_buf` immediately, `f_write` is called less often.

- **Pros:** minimal changes, only `log_writer.c`
- **Cons:** at flush (once a second) the main loop still blocks for the
  entire GC stall. The problem is not eliminated, only spread out over
  time. 256+ KB SRAM does not fit in the F407 main SRAM (128 KB).
- **Verdict:** not suitable.

### B. Double-buffer ping-pong

Two 128 KB buffers. The main loop writes into A while the SD write of
buffer B runs in parallel. When A is full and B's SD write is done — swap.

- **Pros:** the main loop does not block every ms; swaps are rare.
- **Cons:** the swap moment still blocks if the previous write has not
  finished (i.e. a GC stall longer than one buffer's worth of writes).
  256 KB of buffers + still single-loop sync. Ring buffer overflow in
  real CAN still possible.
- **Verdict:** mitigation, not a foundation.

### C. Async SD writes via the DMA TX complete callback

`HAL_SD_WriteBlocks_DMA` starts a transaction and returns immediately;
`HAL_SD_TxCpltCallback` wakes the caller. The main loop sees a "write in
progress" flag and does not block until the next record needs writing.

- **Pros:** the main loop stays alive while DMA runs.
- **Cons:** **does not solve** the main problem — polling CMD13 until
  TRANSFER is still synchronous (`SD_CheckStatusWithTimeout` + the
  already-added `SD_status` retry). During the busy-wait the CPU spins.
- **Verdict:** orthogonal optimisation, not the fix.

### D. Migrate to FreeRTOS + dedicated SD-writer task *(chosen)*

Reproducing the rusEFI architecture on HAL + FreeRTOS:

```
FreeRTOS tasks (priorities):

task_high (osPriorityHigh):
  ├─ can_rx_drain        (ISR → ring buffer → can_map_process → shadow)
  ├─ demo_pack           (in demo mode: waveform → shadow directly
  │                       or via ring buffer)
  └─ high-priority CAN RX ISR pushes frames into a queue/RB

task_log (osPriorityNormal):
  ├─ timer every interval_ms
  ├─ snapshot the shadow buffer → build an MLG record
  └─ put the record into SD_queue (FreeRTOS Queue)

task_sd (osPriorityBelowNormal):
  ├─ read from SD_queue (block on xQueueReceive)
  ├─ write-through into io_buf (512 B — 4 KB)
  ├─ f_write when the buffer is full
  ├─ f_sync every N records
  └─ block in SD_write → during a GC stall ONLY this task blocks,
     task_high keeps draining CAN into the ring buffer
```

- **Pros:**
  - a GC stall (even 710 ms) does not stop CAN RX or data capture
  - extensible: USB CDC → one more task (today it also blocks the main loop)
  - a standard pattern, easy to look up, well documented in CubeMX
  - `taskYIELD()` / `osDelay(1)` inside polling loops solves busy-wait
  - the SD_status retry loop (commit `6d96480`) is already yield-friendly after switching to `osDelay(1)` instead of `HAL_Delay(1)`
- **Cons:**
  - the biggest refactor — `main.c` has to be split into tasks
  - CubeMX regeneration with Middleware → FREERTOS (one more CubeMX-safety concern)
  - task stacks: ~4–8 KB RAM overhead
  - deadlock risks, priority inversion — requires care
  - FatFS must be thread-safe (`FF_FS_REENTRANT=1` in `ffconf.h`)
  - tests: `mlg-test`, snapshot and unit tests stay on the host, but
    runtime behaviour must be validated long-term under load
- **Verdict:** **chosen.** A mature solution that reproduces the
  time-tested rusEFI architecture and removes the root cause.

## Migration plan (rough, not detailed)

Phase 1 — FreeRTOS infrastructure (in CubeMX):

1. Open `.ioc`, enable `Middleware → FREERTOS`
   → `Interface: CMSIS_V2`, `Memory Management: heap_4`
2. Keep one auto-generated default task — the main pipeline will move
   there. Verify CubeMX preserved the USER CODE sections.
3. `FATFS → USE_MUTEX = 1`, `FF_FS_REENTRANT = 1`
4. Rebuild, pass the smoke test (LED blinks, USB CDC works)

Phase 2 — task split:

5. Create `task_sd`, `task_log`; keep the main loop as `task_high`
   (or move it to the default task)
6. Move `lw_tick` into `task_log`
7. Move `f_write/f_sync` into `task_sd`, communicate via a FreeRTOS queue
8. Measure long-term: expect `frames_effective_rate → 99+%`

Phase 3 — polishing:

9. Large io_buf → 512 B (like rusEFI, not needed)
10. `SD_status` retry: `HAL_Delay` → `osDelay`
11. Documentation, tests

## Migration status

**Migration complete.** FreeRTOS CMSIS_V2 is integrated, the pipeline is
split into two tasks (task_producer + task_sd). The smoke test on
`demo_stress_64u16.ini` (64 U16 × 16 CAN IDs × 250 Hz) confirmed:

- `frames_effective_rate` ≈ 100 % (4083 fps vs the expected 4000)
- GC stall `sdw max_lat = 389 ms` is present but **does not block CAN drain**
- `rb count=0` — the ring buffer drains fully even during a stall
- `err=0/0` — no write errors

### What was done

- CubeMX: FreeRTOS CMSIS_V2, TIM6 timebase, heap_4 (16 KB)
- `task_producer` (osPriorityNormal): CAN drain → can_map_process → shadow
  update under osMutex, demo gen, USB CDC CLI, LED. `osDelay(1)` yield.
- `task_sd` (osPriorityBelowNormal): `osDelayUntil` periodic snapshot
  shadow → `lw_write_snapshot` → io_buf → f_write/f_sync/rotate.
  Blocks inside SD_write on a GC stall — only this task waits.
- `SD_status` retry: `HAL_Delay(1)` → `osDelay(1)` (key yield point)
- `SD_write`: `WriteStatus` polling → `osMessageQueueGet` (RTOS DMA
  completion) + `osDelay(1)` in the card-state-wait loop
- `_FS_REENTRANT = 1` (CubeMX auto) — FatFS thread-safe via osSemaphore;
  `cmd_get`/`cmd_ls`/`lw_pause` from task_producer compete with task_sd safely
- Handoff model: snapshot (rusEFI outputChannels pattern), no queue

### Pending

- Long-term stress test on `demo_stress_64u16.ini` (≥ 2 h)
- Max-stress test `demo_stress_128u16.ini` (128 U16 × 1 kHz) — see
  `STRESS_TEST_128U16_PLAN.md`

## Historical position (pre-migration)

- **Problem is real**: 12 % data loss on a 2-hour track — unacceptable
- **Chosen fix**: migrate to FreeRTOS with a dedicated SD-writer task
- **Reference**: rusEFI mmc_card.cpp / thread_priority.h — exactly the
  same architecture, battle-tested over years on the same STM32F4 + SDIO DMA
- **Roadmap**: see `../REQUIREMENTS.md` → v1.0 → "SD writer decoupling"
- **Tests before/after**: see `STRESS_TEST_128U16_PLAN.md` — the final
  max-stress test after migration, current baseline, and the later comparison

## References

- rusEFI `firmware/hw_layer/mmc_card.cpp` — MMCmonThread, 908-964
- rusEFI `firmware/controllers/system/buffered_writer.h` — BufferedWriter<512>
- rusEFI `firmware/controllers/thread_priority.h` — PRIO_MMC = NORMALPRIO-1
- rusEFI `firmware/hw_layer/ports/rusefi_halconf.h:46` — SDC_NICE_WAITING = TRUE
- ChibiOS `os/hal/src/hal_sdc.c` — sdcWrite / PROGRAMMING poll loop
- STM32CubeMX User Manual — FreeRTOS CMSIS_V2 configuration
- FatFS `ffconf.h` — FF_FS_REENTRANT
- `SD_ERRORS.md` — history of the SD GC stall investigation
- `CMD_RSP_TIMEOUT.md` — CMD12 and busy-state polling details
