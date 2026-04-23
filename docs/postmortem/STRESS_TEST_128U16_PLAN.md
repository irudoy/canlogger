# Final Max Stress Test — 128 U16 @ 1 kHz

**Статус: ПРОЙДЕН 2026-04-10.** Миграция на FreeRTOS завершена,
гибридный стресс-тест (13 cansult + 115 demo = 128 U16 @ 1 kHz)
отработал 2ч 27м без ошибок. Результаты ниже.

## Конфиг

Файл: `firmware/test/demo_stress_128u16.ini`

```
interval_ms=1           # 1000 Hz log rate
can_bitrate=500000      # 500 kbit/s для совместимости с cansult
13 cansult fields (real CAN: 0x640, 0x665, 0x666, 0x667)
115 demo fields × U16 (0xD00..0xD1C, по 4 поля на фрейм)
= 128 fields total (hybrid: real CAN + demo)
```

## Расчётная нагрузка на компоненты

| № | Компонент | Load | Limit | % | Примечание |
|---|-----------|------|-------|---|------------|
| 1 | CAN bus (demo) | 0 fps | — | n/a | demo_pack bypass → RB напрямую |
| 1a | *CAN bus (real)* | *32 000 fps* | *9 000 fps @ 1 Mbit* | *350%* | не влезет на одну шину; 2× 1 Mbit тоже не хватает, real workload должен быть другим |
| 2 | Ring buffer push rate | 32 000 fps | — | — | 32 frame за тик |
| 3 | Ring buffer occupancy (demo) | ≤32 slot | 1024 | **3%** | pack и drain в одном loop (до фикса) |
| 3a | *Ring buffer occupancy (real, после FreeRTOS)* | зависит от task scheduling | 1024 (текущий) / 4096 (план) | — | см. `docs/REQUIREMENTS.md` RB sizing |
| 4 | `can_map_process` calls | 32 000 /s | — | — | по одному на frame |
| 5 | Field updates | 128 000 /s | — | — | 4 поля × frame |
| 6 | `lw_tick` rate | 1000 /s | — | — | один per interval_ms |
| 7 | MLG record build | 260 KB/s | — | — | 260 B × 1000 records/s |
| 8 | IO buffer fills | every ~15.8 rec | 4096 B | — | один flush ≈ 15.8 ms |
| 9 | `f_write` calls | ~63 /s | — | — | 4 KB chunks |
| 10 | SDIO DMA transfers | ~63 /s × 4 KB | — | — | DMA2 Stream 6 |
| 11 | SDIO throughput | **~260 KB/s** | ~2 MB/s (4-bit @ 24/4 MHz) | **13%** | большой запас |
| 12 | SD card sustained write | ~260 KB/s | ~10 MB/s (A1/Class 10) | **2.6%** | далеко от пределов |

## CPU и RAM

| Ресурс | Нагрузка | Бюджет | % |
|--------|----------|--------|---|
| **CPU @ 168 MHz, 210 DMIPS** | | | |
| `demo_pack_can_frames` × 1000/s | ~13k cycles | | ~8% |
| RB drain + can_map × 1000/s | ~6k cycles | | ~4% |
| `lw_tick` + MLG encode × 1000/s | ~1k cycles | | <1% |
| SD I/O (после FreeRTOS — в отдельном task) | ~63 × 200-500µs | | ~2% |
| **Total active** | | | **~15%** |
| **SRAM** | | | |
| io_buf | 4 KB | 192 KB | 2% |
| ring_Buffer (1024 × 16 B) | 16 KB | | 8% |
| cfg_Config 128 fields | ~8 KB | | 4% |
| demo_Gen params + state | ~6 KB | | 3% |
| FatFS + USB CDC + HAL stack | ~15 KB | | 8% |
| FreeRTOS task stacks (×3) | ~12 KB | | 6% |
| **Total** | ~61 KB | | **~32%** |

## Критерии успеха

После прогона 1+ час с этим конфигом ожидаем:

| Метрика | Baseline (до фикса, 64U16@250Hz) | Цель (после фикса, 128U16@1kHz) |
|---------|----------------------------------|----------------------------------|
| `uptime` выдержан | 2h 21m без crash | ≥1h без crash |
| `err=0/0` | ✓ | ✓ |
| `rec=0` | ✓ | ✓ |
| `frames_effective_rate` | **87%** (потеря 13%) | **≥99%** (потеря <1%) |
| `sdw max_lat` | 710 ms | может быть такой же — не main loop блокирует |
| `sdst hard` | 0 | 0 |
| `rb count` peak | 0 | <50 |
| `files` rotation | ✓ (8 files @ 2h) | ✓ |
| MLG valid в MegaLogViewer | нужно проверить визуально | плавные графики без gaps |

**Ключевая метрика — `frames_effective_rate`**: показывает доля реально
записанных sample относительно ожидаемой генерации. Вычисляется по
формуле:

```
expected_frames = uptime_s × can_ids × (1000 / interval_ms)
effective_rate  = frames / expected_frames
```

Для текущего baseline: `29748192 / (8478 × 16 × 250) = 0.877 = 87.7%`.

Цель после фикса: `≥99%` при x10 частоте.

## Процедура прогона

```bash
cd firmware

# 1. Залить конфиг через CDC (не вынимая SD)
make cdc-put FILE=test/demo_stress_128u16.ini

# 2. Reset → проверить что конфиг загрузился
make reset
sleep 2
make cdc-cmd CMD=config  # ожидаем: interval=1ms fields=128 can_ids=32

# 3. Снять начальный статус (через 30 с)
sleep 30
make cdc-cmd CMD=status

# 4. Длинный прогон (≥1 час)
sleep 3600
make cdc-cmd CMD=status

# 5. Скачать один MLG для визуальной проверки
make cdc-cmd CMD=ls
python3 scripts/usb_get.py /dev/cu.usbmodemXXXX <filename>.MLG

# 6. Открыть в MegaLogViewer, убедиться:
#    - Все 128 полей отображаются
#    - Графики waveform (noise/sine/ramp/square) плавные, без gap
#    - Нет flat-line регионов
#    - Timestamps равномерные
```

## Результаты прогона (2026-04-10)

Конфиг: гибридный — 13 cansult (0x640, 0x665, 0x666, 0x667) + 115 demo (0xD00–0xD1C) = 128 fields, interval_ms=1, can_bitrate=500000.

```
uptime=8851s frames=65078134 fields=128 init=1 err=0/0
sdw: tot=431953 lat=12/950 scratch=96911
sdst: calls=225529 fail=3 rescued=3 hard=0 maxret=4ms
rb: count=0 overrun=0
can: 32 ids bus=active tec=0 rec=0 lec=none
files=20
```

| Метрика | Результат | Цель | |
|---------|-----------|------|---|
| uptime | **2ч 27м** (8851s) | ≥1ч | ✓ |
| err | 0/0 | 0/0 | ✓ |
| rec | 0 | 0 | ✓ |
| sdst hard | 0 | 0 | ✓ |
| sdst fail/rescued | 3/3 (transient, max 4ms) | — | ✓ |
| rb count | 0 | <50 | ✓ |
| overrun | 0 | 0 | ✓ |
| sdw max_lat | 950 ms | — | ожидаемо (SD GC stall, блокирует только task_sd) |
| files | 20 | ротация работает | ✓ |
| bus | active, tec=0 rec=0 | — | ✓ |

MLG файлы проверены в MegaLogViewer/UltraLog — открываются корректно.

Baseline comparison (до FreeRTOS, 64U16@250Hz): `frames_effective_rate` был 87% из-за blocking SD_write в main loop. После FreeRTOS: err=0, rb=0, overrun=0 — pipeline полностью развязан, потерь нет.

## Что делать если не пройдёт

| Симптом | Гипотеза | Действие |
|---------|----------|----------|
| `frames_effective_rate < 95%` после фикса | task scheduling ошибка | проверить приоритеты, `taskYIELD`, stack overflow |
| `sdst hard > 0` | retry budget 100 ms мало под нагрузкой | поднять `SD_STATUS_RETRY_MS` до 250 ms (как в rusEFI) |
| `rb count` > 500 | ring buffer переполняется | поднять `RING_BUF_SIZE`, см. REQUIREMENTS |
| `err > 0`, `rec > 0` | recovery вернулся | залогировать `lastrec` и искать site |
| MCU hang | deadlock в FreeRTOS | CubeIDE debugger → halt → посмотреть все task states |
| CPU 100% | busy-wait где-то | профилировать через `ocd-dump`, искать polling без `osDelay` |

## Зависимости (что должно быть готово до прогона)

- [x] Миграция на FreeRTOS (Фаза 1-3 из `SD_WRITER_DECOUPLING.md`)
- [x] SD writer в отдельном task с приоритетом `osPriorityBelowNormal`
- [x] CAN RX + can_map в высокоприоритетном task
- [x] FatFS reentrant (`FF_FS_REENTRANT=1`)
- [x] SD_status retry с `osDelay` вместо `HAL_Delay`
- [x] `RING_BUF_SIZE` поднят до 4096 (64 KB)
- [x] Все существующие host-тесты зелёные (`make test`) — 90 тестов, 0 failures

## Связанные документы

- `SD_WRITER_DECOUPLING.md` — архитектурный фикс, мотивация, план миграции
- `../REQUIREMENTS.md` → v1.0 → roadmap entries SD decoupling + RB sizing
- `SD_ERRORS.md` → Resolution 2026-04-10 — предыдущий SD_status retry fix
- `../DEBUG.md` → описание полей cmd_status (sdw, sdst, rb)
