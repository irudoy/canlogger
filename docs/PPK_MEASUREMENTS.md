# PPK2 Power Measurements — Supercap Sizing

**Дата: 2026-04-10**

Измерения тока потребления STM32_F4VE V2.0 + CAN transceiver для
расчёта суперконденсатора graceful shutdown.

## Оборудование

- Nordic PPK2 (Power Profiler Kit II), source mode @ 5.0V
- PPK2 подключён вместо USB питания (5V → VCC платы, GND → GND)
- Logic port: D1 = PA6 (LED D2, запись), D2 = PA7 (LED D3, shutdown)
- USB CDC отключён (чтобы не питать плату через USB)
- Конфиг: `demo_stress_128u16.ini` (128 U16, 32 CAN ID, 1 ms interval)

## Методика

1. PPK2 source mode подаёт 5V на плату
2. Плата стартует, читает конфиг, начинает запись → LED D2 ON
3. Нажатие K1 → ISR мгновенно гасит D2 (маркер на logic port)
4. task_sd завершает: flush_io_buf → f_sync → f_close → LED D3 ON
5. t_shutdown = время между D2 OFF и D3 ON

## Результаты

### Рабочий ток (streaming CAN + SD write)

| Параметр | Значение |
|----------|----------|
| I_avg | **94 mA** |
| I_peak | **211 mA** (startup spike) |
| I_peak (SD writes) | **~192 mA** |

### Shutdown

| Параметр | Короткая запись (3.5s) | Длинная запись (3+ min) |
|----------|------------------------|------------------------|
| t_shutdown | 123 ms | 149 ms |
| I_avg | 109 mA | 101 mA |
| I_peak | 164 mA | 192 mA |
| Charge | 13.4 mC | 15.1 mC |

### Idle (после shutdown, MCU работает)

| Параметр | Значение |
|----------|----------|
| I_idle | **89 mA** |

## Расчёт суперконденсатора

### Входные данные

```
V_charged    = 5.0V (DC-DC output)
V_min_board  = 4.4V (AMS1117-3.3 dropout ~1.1V @ full load)
ΔV           = 0.6V

I_shutdown   = 110 mA (worst case avg)
I_peak       = 192 mA (worst case peak)
t_shutdown   = 150 ms (типичный flush)
t_gc_stall   = 950 ms (worst case из стресс-теста, sdw max_lat)
t_total      = 1.1 s (flush + GC stall worst case)
```

### Формулы

```
C_min  = I × t / ΔV = 0.110 × 1.1 / 0.6 = 0.20 F
C_safe = C_min × 2 = 0.40 F (запас ×2)
ESR    < ΔV / I_peak = 0.6 / 0.192 = 3.1 Ω
```

### Рекомендация

| Параметр | Минимум | Рекомендация |
|----------|---------|-------------|
| Ёмкость | 0.40 F | **0.47 F** (стандартный номинал) |
| Напряжение | ≥ 5.5V | **5.5V** |
| ESR | < 3.1 Ω | < 3 Ω |
| Ток утечки | < 10 мкА | — |

Стандартный **0.47F 5.5V** supercap подходит впритык.
**1F 5.5V** даёт запас ×2.5 на случай нескольких GC stalls подряд.

## Артефакты

```
docs/ppk-measurements/
├── ppk-20260410T144313.csv  — CSV данные (полный цикл: startup → запись → shutdown)
├── ppk-20260410T144323.png  — скриншот PPK2: полный цикл
├── ppk-20260410T144636.png  — скриншот PPK2: shutdown (короткая запись)
└── ppk-20260410T145153.png  — скриншот PPK2: shutdown (длинная запись)
```

## Связанные документы

- `docs/HARDWARE.md` — схема hat с supercap и VIN_SENSE
- `docs/postmortem/STRESS_TEST_128U16_PLAN.md` — стресс-тест (sdw max_lat = 950 ms)
- `docs/REQUIREMENTS.md` → v1.0 → Graceful shutdown
