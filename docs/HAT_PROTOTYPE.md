# Hat prototype — graceful-shutdown breadboard

**Date: 2026-04-11**

Breadboard build to validate graceful shutdown: DC-DC + supercap + VIN_SENSE.
The CAN transceiver stays on its own board (same as today).

## Components

| Function | Component | Package | ID |
|----------|-----------|---------|----|
| DC-DC 12 V → 5 V | Off-the-shelf module (220 µF 35 V on the output) | — | — |
| Supercap | KAMCAP 2.5 F 5.5 V | Radial, polarised | — |
| Supercap isolation | 1N5819 Schottky 40 V 1 A | DO-41 | D004 |
| VIN_SENSE top | 10 kΩ | TH | R028 |
| VIN_SENSE bottom | 10 kΩ | TH | R028 |

## Schematic

```
                         ┌───────────────┐
  12V (car) ─────────────┤  DC-DC module ├──── 5V_DC
                         └──────┬────────┘        │
                                │           D004  │  1N5819
                                │         ───────►|────────
                                │        anode        cathode
                                │       (DC-DC)    (stripe)
                                │                     │
                                │                  5V_CAP
                                │                     │
                                │         ┌───────────┼──────────┐
                                │         │           │          │
                                │       ┌─┴──┐   Board VCC       │
                                │       │CAP │   (pin header)    │
                                │       │2.5F│                   │
                                │       │5.5V│  (+) up           │
                                │       └─┬──┘  (−) stripe down  │
                                │         │                      │
                                │        GND                    GND
                                │
                             ┌──┴──┐
                             │10kΩ │ R028
                             └──┬──┘
                                │
                                ├──── VIN_SENSE → PA0 (ADC1_IN0)
                                │
                             ┌──┴──┐
                             │10kΩ │ R028
                             └──┬──┘
                                │
                               GND
```

## 1N5819 diode — orientation

```
  DC-DC 5V ──── anode │▶  cathode ──── supercap + board VCC
                      ─────
                      stripe on the cathode (supercap side)
```

The stripe on the package = cathode = supercap/board side.
Current flows from DC-DC to the board. When 12 V disappears the diode closes and the supercap powers the board.

## Supercap KAMCAP 2.5 F 5.5 V — polarity

- Stripe on the can = **negative** (to GND)
- Long leg = **positive** (to 5V_CAP)

## VIN_SENSE divider — calculation

Tap from 5V_DC (DC-DC output, before the diode). The DC-DC module already filters automotive transients, so no extra protection is needed.

Divider 10 kΩ + 10 kΩ = 1:2.

```
V_adc = V_dc × 10 / (10 + 10) = V_dc / 2

V_dc = 5.0 V (nominal)  → V_adc = 2.50 V
V_dc = 4.5 V            → V_adc = 2.25 V
V_dc = 4.0 V            → V_adc = 2.00 V
V_dc = 0.0 V (lost)     → V_adc = 0.00 V
```

The shutdown threshold is chosen in firmware — dial it in experimentally.
Expected behaviour: V_dc drops quickly to 0 when 12 V is lost (the supercap does not hold that node; the diode is closed).

## Operating logic

1. **Power present**: 12 V → DC-DC → 5 V → through the diode → supercap charges to ~4.6 V (5 V minus the diode Vf ~0.4 V); the board runs on 5 V.
2. **Power lost**: the DC-DC stops, 5V_DC drops to 0; the diode closes; the board lives off the supercap (4.6 V → 4.4 V min).
3. **Detection**: ADC on PA0 reads VIN_SENSE, sees the drop → `lw_shutdown = 1`.
4. **Shutdown**: task_sd performs flush → f_sync → f_close; LED D3 ON.
5. **Margin**: 2.5 F × 0.2 V / 0.110 A ≈ **4.5 s** (from 4.6 V to the 4.4 V minimum).

## TODO

- [ ] Configure PA0 as an ADC in CubeMX
- [ ] Assemble the prototype
- [ ] Verify supercap charging (time, voltage)
- [ ] Verify shutdown: pull 12 V, confirm the MLG closes cleanly
- [ ] Pick the VIN_SENSE threshold for firmware

## Related documents

- `docs/PPK_MEASUREMENTS.md` — current measurements for supercap sizing
- `docs/HARDWARE.md` — hat section, pins
- `docs/REQUIREMENTS.md` → v1.0 → Graceful shutdown
