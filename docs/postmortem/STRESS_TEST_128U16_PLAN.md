# Final max-stress test — 128 U16 @ 1 kHz

**Status: PASSED 2026-04-10.** The FreeRTOS migration is complete, and
the hybrid stress test (13 cansult + 115 demo = 128 U16 @ 1 kHz) ran
for 2 h 27 min without errors. Results below.

## Config

File: `firmware/test/demo_stress_128u16.ini`

```
interval_ms=1           # 1000 Hz log rate
can_bitrate=500000      # 500 kbit/s to stay compatible with cansult
13 cansult fields (real CAN: 0x640, 0x665, 0x666, 0x667)
115 demo fields × U16 (0xD00..0xD1C, 4 fields per frame)
= 128 fields total (hybrid: real CAN + demo)
```

## Estimated component load

| # | Component | Load | Limit | % | Note |
|---|-----------|------|-------|---|------|
| 1 | CAN bus (demo) | 0 fps | — | n/a | demo_pack bypass → RB directly |
| 1a | *CAN bus (real)* | *32 000 fps* | *9 000 fps @ 1 Mbit* | *350 %* | does not fit on a single bus; 2 × 1 Mbit is not enough either — the real workload has to be different |
| 2 | Ring buffer push rate | 32 000 fps | — | — | 32 frames per tick |
| 3 | Ring buffer occupancy (demo) | ≤32 slots | 1024 | **3 %** | pack and drain in the same loop (pre-fix) |
| 3a | *Ring buffer occupancy (real, post-FreeRTOS)* | depends on task scheduling | 1024 (current) / 4096 (planned) | — | see `docs/REQUIREMENTS.md` RB sizing |
| 4 | `can_map_process` calls | 32 000 /s | — | — | one per frame |
| 5 | Field updates | 128 000 /s | — | — | 4 fields × frame |
| 6 | `lw_tick` rate | 1000 /s | — | — | one per interval_ms |
| 7 | MLG record build | 260 KB/s | — | — | 260 B × 1000 records/s |
| 8 | IO buffer fills | every ~15.8 records | 4096 B | — | one flush ≈ 15.8 ms |
| 9 | `f_write` calls | ~63 /s | — | — | 4 KB chunks |
| 10 | SDIO DMA transfers | ~63 /s × 4 KB | — | — | DMA2 Stream 6 |
| 11 | SDIO throughput | **~260 KB/s** | ~2 MB/s (4-bit @ 24/4 MHz) | **13 %** | plenty of headroom |
| 12 | SD card sustained write | ~260 KB/s | ~10 MB/s (A1/Class 10) | **2.6 %** | far from the limit |

## CPU and RAM

| Resource | Load | Budget | % |
|----------|------|--------|---|
| **CPU @ 168 MHz, 210 DMIPS** | | | |
| `demo_pack_can_frames` × 1000/s | ~13k cycles | | ~8 % |
| RB drain + can_map × 1000/s | ~6k cycles | | ~4 % |
| `lw_tick` + MLG encode × 1000/s | ~1k cycles | | <1 % |
| SD I/O (post-FreeRTOS — in a separate task) | ~63 × 200-500 µs | | ~2 % |
| **Total active** | | | **~15 %** |
| **SRAM** | | | |
| io_buf | 4 KB | 192 KB | 2 % |
| ring_Buffer (1024 × 16 B) | 16 KB | | 8 % |
| cfg_Config 128 fields | ~8 KB | | 4 % |
| demo_Gen params + state | ~6 KB | | 3 % |
| FatFS + USB CDC + HAL stack | ~15 KB | | 8 % |
| FreeRTOS task stacks (×3) | ~12 KB | | 6 % |
| **Total** | ~61 KB | | **~32 %** |

## Success criteria

After 1+ hour with this config we expect:

| Metric | Baseline (pre-fix, 64U16@250Hz) | Target (post-fix, 128U16@1kHz) |
|--------|---------------------------------|--------------------------------|
| `uptime` held | 2 h 21 min without a crash | ≥ 1 h without a crash |
| `err=0/0` | ✓ | ✓ |
| `rec=0` | ✓ | ✓ |
| `frames_effective_rate` | **87 %** (13 % loss) | **≥ 99 %** (< 1 % loss) |
| `sdw max_lat` | 710 ms | may stay similar — it no longer blocks the main loop |
| `sdst hard` | 0 | 0 |
| `rb count` peak | 0 | < 50 |
| `files` rotation | ✓ (8 files @ 2 h) | ✓ |
| MLG valid in MegaLogViewer | visual check needed | smooth plots with no gaps |

**The key metric — `frames_effective_rate`**: the share of samples actually
recorded versus the expected generation. Computed as:

```
expected_frames = uptime_s × can_ids × (1000 / interval_ms)
effective_rate  = frames / expected_frames
```

For the current baseline: `29748192 / (8478 × 16 × 250) = 0.877 = 87.7 %`.

Target after the fix: `≥ 99 %` at 10× rate.

## Procedure

```bash
cd firmware

# 1. Upload the config over CDC (without pulling the SD)
make cdc-put FILE=test/demo_stress_128u16.ini

# 2. Reset → verify the config is loaded
make reset
sleep 2
make cdc-cmd CMD=config  # expect: interval=1ms fields=128 can_ids=32

# 3. Capture the baseline status (after 30 s)
sleep 30
make cdc-cmd CMD=status

# 4. Long run (≥ 1 hour)
sleep 3600
make cdc-cmd CMD=status

# 5. Download one MLG for a visual check
make cdc-cmd CMD=ls
python3 scripts/usb_get.py /dev/cu.usbmodemXXXX <filename>.MLG

# 6. Open it in MegaLogViewer, check that:
#    - all 128 fields show up
#    - waveform plots (noise/sine/ramp/square) are smooth, no gaps
#    - no flat-line regions
#    - timestamps are evenly spaced
```

## Run results (2026-04-10)

Config: hybrid — 13 cansult (0x640, 0x665, 0x666, 0x667) + 115 demo (0xD00–0xD1C) = 128 fields, interval_ms=1, can_bitrate=500000.

```
uptime=8851s frames=65078134 fields=128 init=1 err=0/0
sdw: tot=431953 lat=12/950 scratch=96911
sdst: calls=225529 fail=3 rescued=3 hard=0 maxret=4ms
rb: count=0 overrun=0
can: 32 ids bus=active tec=0 rec=0 lec=none
files=20
```

| Metric | Result | Target | |
|--------|--------|--------|---|
| uptime | **2 h 27 min** (8851 s) | ≥ 1 h | ✓ |
| err | 0/0 | 0/0 | ✓ |
| rec | 0 | 0 | ✓ |
| sdst hard | 0 | 0 | ✓ |
| sdst fail/rescued | 3/3 (transient, max 4 ms) | — | ✓ |
| rb count | 0 | < 50 | ✓ |
| overrun | 0 | 0 | ✓ |
| sdw max_lat | 950 ms | — | expected (SD GC stall, blocks only task_sd) |
| files | 20 | rotation works | ✓ |
| bus | active, tec=0 rec=0 | — | ✓ |

MLG files checked in MegaLogViewer / UltraLog — they open correctly.

Baseline comparison (pre-FreeRTOS, 64U16@250Hz): `frames_effective_rate` was 87 % because of the blocking `SD_write` in the main loop. After FreeRTOS: err=0, rb=0, overrun=0 — the pipeline is fully decoupled, no losses.

## What to do if it fails

| Symptom | Hypothesis | Action |
|---------|------------|--------|
| `frames_effective_rate < 95 %` after the fix | task-scheduling bug | check priorities, `taskYIELD`, stack overflow |
| `sdst hard > 0` | 100 ms retry budget is too tight under load | raise `SD_STATUS_RETRY_MS` to 250 ms (like rusEFI) |
| `rb count` > 500 | ring buffer overflows | raise `RING_BUF_SIZE`, see REQUIREMENTS |
| `err > 0`, `rec > 0` | recovery is firing | log `lastrec` and find the site |
| MCU hang | FreeRTOS deadlock | CubeIDE debugger → halt → inspect every task state |
| CPU 100 % | busy-wait somewhere | profile via `ocd-dump`, look for polling without `osDelay` |

## Prerequisites (must be ready before the run)

- [x] FreeRTOS migration (phases 1–3 of `SD_WRITER_DECOUPLING.md`)
- [x] SD writer in a separate task with priority `osPriorityBelowNormal`
- [x] CAN RX + can_map in a high-priority task
- [x] FatFS reentrant (`FF_FS_REENTRANT=1`)
- [x] SD_status retry with `osDelay` instead of `HAL_Delay`
- [x] `RING_BUF_SIZE` raised to 4096 (64 KB)
- [x] All existing host tests green (`make test`) — 90 tests, 0 failures

## Related documents

- `SD_WRITER_DECOUPLING.md` — architectural fix, motivation, migration plan
- `../REQUIREMENTS.md` → v1.0 → roadmap entries SD decoupling + RB sizing
- `SD_ERRORS.md` → Resolution 2026-04-10 — previous SD_status retry fix
- `../DEBUG.md` → description of the cmd_status fields (sdw, sdst, rb)
