# PPK2 Power Measurements — supercap sizing

**Date: 2026-04-10**

Current-draw measurements for the STM32_F4VE V2.0 + CAN transceiver, used to size the graceful-shutdown supercap.

## Equipment

- Nordic PPK2 (Power Profiler Kit II), source mode @ 5.0 V
- PPK2 wired in place of USB power (5 V → board VCC, GND → GND)
- Logic port: D1 = PA6 (LED D2, recording), D2 = PA7 (LED D3, shutdown)
- USB CDC disconnected (so the board is not powered through USB)
- Config: `demo_stress_128u16.ini` (128 U16, 32 CAN IDs, 1 ms interval)

## Method

1. PPK2 source mode feeds 5 V into the board
2. Board starts, reads the config, starts recording → LED D2 ON
3. Pressing K1 → ISR immediately turns D2 OFF (marker on the logic port)
4. task_sd finishes: flush_io_buf → f_sync → f_close → LED D3 ON
5. t_shutdown = time between D2 OFF and D3 ON

## Results

### Active current (streaming CAN + SD write)

| Parameter | Value |
|-----------|-------|
| I_avg | **94 mA** |
| I_peak | **211 mA** (startup spike) |
| I_peak (SD writes) | **~192 mA** |

### Shutdown

| Parameter | Short run (3.5 s) | Long run (3+ min) |
|-----------|-------------------|-------------------|
| t_shutdown | 123 ms | 149 ms |
| I_avg | 109 mA | 101 mA |
| I_peak | 164 mA | 192 mA |
| Charge | 13.4 mC | 15.1 mC |

### Idle (after shutdown, MCU still running)

| Parameter | Value |
|-----------|-------|
| I_idle | **89 mA** |

## Supercap sizing

### Inputs

```
V_charged    = 5.0 V (DC-DC output)
V_min_board  = 4.4 V (AMS1117-3.3 dropout ~1.1 V @ full load)
ΔV           = 0.6 V

I_shutdown   = 110 mA (worst-case average)
I_peak       = 192 mA (worst-case peak)
t_shutdown   = 150 ms (typical flush)
t_gc_stall   = 950 ms (worst case from stress test, sdw max_lat)
t_total      = 1.1 s (flush + GC stall, worst case)
```

### Formulas

```
C_min  = I × t / ΔV = 0.110 × 1.1 / 0.6 = 0.20 F
C_safe = C_min × 2 = 0.40 F (2× margin)
ESR    < ΔV / I_peak = 0.6 / 0.192 = 3.1 Ω
```

### Recommendation

| Parameter | Minimum | Recommended |
|-----------|---------|-------------|
| Capacitance | 0.40 F | **0.47 F** (standard value) |
| Voltage | ≥ 5.5 V | **5.5 V** |
| ESR | < 3.1 Ω | < 3 Ω |
| Leakage | < 10 µA | — |

A standard **0.47 F 5.5 V** supercap fits with no margin.
**1 F 5.5 V** gives ~2.5× margin to cover multiple consecutive GC stalls.

## Artifacts

```
docs/ppk-measurements/
├── ppk-20260410T144313.csv  — CSV data (full cycle: startup → recording → shutdown)
├── ppk-20260410T144323.png  — PPK2 screenshot: full cycle
├── ppk-20260410T144636.png  — PPK2 screenshot: shutdown (short run)
└── ppk-20260410T145153.png  — PPK2 screenshot: shutdown (long run)
```

## Related documents

- `docs/HARDWARE.md` — hat schematic with supercap and VIN_SENSE
- `docs/postmortem/STRESS_TEST_128U16_PLAN.md` — stress test (sdw max_lat = 950 ms)
- `docs/REQUIREMENTS.md` → v1.0 → Graceful shutdown
