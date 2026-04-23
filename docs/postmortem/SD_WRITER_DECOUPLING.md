# SD Writer Decoupling — Main Loop Blocks on GC Stalls

## Симптом

На 2 ч 21 мин прогоне `demo_stress_64u16.ini` (64 U16 × 16 CAN IDs × 250 Hz)
зафиксированы следующие показатели:

```
uptime=8478s frames=29748192 fields=64 init=1 err=0/0
sdw: tot=195401 lat=2/710 scratch=42044
sdst: calls=103401 fail=80 rescued=80 hard=0 maxret=8ms
```

При ожидаемой генерации **4000 фреймов/с** (16 CAN IDs × 250 Hz) реальный
темп — **3510 fps** (29.7M / 8478s). Дефицит **~12%**, что эквивалентно
**~17 мин пропущенных данных на 2-часовом треке**.

Причина: `err=0/0` и `rec=0` показывают что pipeline не падает, но
`sdw max_lat = 710 ms` — один из GC stalls длился почти секунду, и **во
время этого stall main loop полностью стоит внутри `SD_write`**. За 710 мс
не выполняется ни `demo_pack_can_frames`, ни `can_map_process`, ни
`lw_tick` → в MLG файле появляется **gap в 710 мс (~177 sample при 250 Hz)**.
Это не IO error, но **honest data loss** с точки зрения потребителя
(MegaLogViewer увидит flat line или jump).

На max-нагрузке 128 U16 × 1 kHz loss растянется пропорционально — запись
становится непригодной для production.

## Root cause: архитектура bare-metal single-loop

Весь pipeline синхронный, в одном `while (1)`:

```
main loop:
  ├─ demo_pack_can_frames  (или CAN ISR drain через ring buffer)
  ├─ can_map_process
  ├─ lw_tick → f_write → SD_write → DMA start → wait DMA IRQ
  │                                            └─ poll CMD13 до TRANSFER
  │                                                  └─ если карта в PROGRAMMING:
  │                                                      цикл до 710 ms
  └─ debug_cmd_poll
```

`SD_write` блокирующий. Пока карта не вернётся в TRAN state, никакие
другие задачи не исполняются. Это фундаментальное ограничение
single-loop дизайна — смягчить можно только bигим буфером и/или
архитектурной перестройкой.

## Сравнение с rusEFI (референс)

rusEFI решает ровно эту же проблему на STM32F4 + ChibiOS. Ключевые
приёмы (см. `~/src/oss/rusefi/firmware/hw_layer/mmc_card.cpp:908-964`):

1. **Выделенный SD thread `MMCmonThread`** с приоритетом
   `PRIO_MMC = NORMALPRIO-1` — **ниже всего остального**
   (`thread_priority.h:43`). Стек `3 × UTILITY_THREAD_STACK_SIZE`.
   Main loop (`NORMALPRIO+10`), CAN RX (`NORMALPRIO+6`), ADC — все
   приоритетнее. Во время SD stall блокируется только SD thread;
   данные продолжают собираться.

2. **Shared `outputChannels` struct** вместо очереди. Main loop
   непрерывно обновляет поля. SD thread, когда его разбудят, делает
   snapshot текущего состояния и пишет один MLG record. Промежуточные
   значения "теряются" как timing gap, но каждая записанная точка —
   свежая. Нет overflow, нет дропов — просто другая частота sample.

3. **`SDC_NICE_WAITING = TRUE`** в `rusefi_halconf.h:46`. ChibiOS `sdc_lld`
   во время polling PROGRAMMING state вызывает
   `osalThreadSleepMilliseconds(1)` вместо busy-wait — отдаёт scheduler'у
   каждую миллисекунду. Другие threads исполняются даже *внутри* GC
   stall.

4. **`f_expand(fd, 32 MB, 1)`** pre-allocation при открытии файла
   (`mmc_card.cpp:395-401`) — исключает FAT updates и самые тяжёлые
   GC stalls. **Мы это уже делаем (4 MB)**.

5. **Single 512 B buffer** (`BufferedWriter<512>` в
   `buffered_writer.h:12`) — просто, потому что thread-архитектура
   уже всё решает. Нет ping-pong, нет большого io_buf.

6. **`f_sync` раз в 10 f_write** (`F_SYNC_FREQUENCY = 10`,
   `mmc_card.cpp:30`). При 4 write/s = один f_sync в ~2.5 s.

**Главный вывод**: рабочая архитектура — **"блокируй только SD-поток,
не весь pipeline"**. Всё остальное (buffer sizes, sync frequency,
pre-allocation) — мелочи поверх этого.

## Варианты решения

### A. Больший синхронный I/O буфер (4 KB → 256+ KB)

Main loop пишет в `io_buf` моментально, `f_write` дёргается реже.

- **Плюсы:** минимальные изменения, только `log_writer.c`
- **Минусы:** при flush (раз в 1 сек) всё равно блокирует main loop
  на весь GC stall. Проблема не устраняется, только размазывается по
  времени. 256+ KB SRAM не влезает в main SRAM F407 (128 KB).
- **Вердикт:** не подходит.

### B. Double-buffer ping-pong

Два буфера по 128 KB. Main loop пишет в A, параллельно идёт SD write
буфера B. Когда A заполнен и B завершил SD — swap.

- **Плюсы:** main loop не блокирует на каждом ms; swap редкий.
- **Минусы:** swap-момент всё равно блокирует если предыдущая запись не
  завершилась (i.e. GC stall дольше чем заполнение одного буфера).
  256 KB на буферы + всё ещё single-loop sync. Ring buffer
  переполнение в real CAN остаётся.
- **Вердикт:** смягчение, не фундамент.

### C. Async SD writes через DMA TX complete callback

`HAL_SD_WriteBlocks_DMA` запускает транзакцию, возвращает сразу;
`HAL_SD_TxCpltCallback` будит заказчика. Main loop видит "write in
progress" флаг и не блокируется, пока не нужно записать следующее.

- **Плюсы:** main loop живёт, пока DMA крутится.
- **Минусы:** **не решает** главную проблему — polling CMD13 до
  TRANSFER всё равно синхронный (`SD_CheckStatusWithTimeout` + уже
  внедрённый `SD_status` retry). Во время busy-wait CPU гоняется.
- **Вердикт:** ортогональный optimization, не фикс.

### D. Миграция на FreeRTOS + dedicated SD writer task *(выбранный)*

Повторение rusEFI-архитектуры на HAL + FreeRTOS:

```
FreeRTOS tasks (приоритеты):

task_high (osPriorityHigh):
  ├─ can_rx_drain        (ISR → ring buffer → can_map_process → shadow)
  ├─ demo_pack           (в demo mode: waveform → shadow напрямую
  │                       или через ring buffer)
  └─ высокоприоритетная CAN RX ISR кладёт фреймы в queue/RB

task_log (osPriorityNormal):
  ├─ таймер каждые interval_ms
  ├─ snapshot shadow buffer → собрать MLG record
  └─ положить record в SD_queue (FreeRTOS Queue)

task_sd (osPriorityBelowNormal):
  ├─ чтение из SD_queue (block на xQueueReceive)
  ├─ write-through в io_buf (512 B — 4 KB)
  ├─ f_write когда буфер полон
  ├─ f_sync каждые N записей
  └─ block в SD_write → во время GC stall стоит ТОЛЬКО этот task,
     task_high продолжает дренить CAN в ring buffer
```

- **Плюсы:**
  - GC stall (даже 710 мс) не останавливает CAN RX и data capture
  - расширяемо: USB CDC → ещё один task (сейчас тоже блокирует main loop)
  - стандартный паттерн, гуглится, хорошо документирован в CubeMX
  - `taskYIELD()` / `osDelay(1)` внутри polling loops решает busy-wait
  - SD_status retry loop (commit `6d96480`) уже yield-friendly после перехода на `osDelay(1)` вместо `HAL_Delay(1)`
- **Минусы:**
  - самый большой рефакторинг — нужно переразбить `main.c` на tasks
  - CubeMX регенерация с Middleware → FREERTOS (ещё один CubeMX-safe issue)
  - стеки для tasks: ~4-8 KB RAM overhead
  - deadlock-risks, priority inversion — нужно внимательно
  - FatFS должен быть thread-safe (`FF_FS_REENTRANT=1` в `ffconf.h`)
  - тесты: `mlg-test`, snapshot и unit всё ещё на хосте, но runtime
    поведение должно быть протестировано long-term под нагрузкой
- **Вердикт:** **выбран.** Зрелое решение, воспроизводит проверенную
  временем архитектуру rusEFI, убирает коренную причину.

## План миграции (черновой, не детальный)

Фаза 1 — инфраструктура FreeRTOS (в CubeMX):

1. Открыть `.ioc`, включить `Middleware → FREERTOS`
   → `Interface: CMSIS_V2`, `Memory Management: heap_4`
2. Оставить одну auto-generated default task — туда перенесётся
   основной pipeline. Проверить что CubeMX сохранил USER CODE sections.
3. `FATFS → USE_MUTEX = 1`, `FF_FS_REENTRANT = 1`
4. Пересобрать, пройти smoke test (LED мигает, USB CDC работает)

Фаза 2 — разделение tasks:

5. Создать `task_sd`, `task_log`, оставить main loop как `task_high`
   (или перенести в default task)
6. Вынести `lw_tick` в `task_log`
7. Вынести `f_write/f_sync` в `task_sd`, общение через FreeRTOS queue
8. Замерить long-term: ожидаем `frames_effective_rate → 99+%`

Фаза 3 — polishing:

9. Большие io_buf → 512 B (как rusEFI, не нужен)
10. `SD_status` retry: `HAL_Delay` → `osDelay`
11. Документация, тесты

## Статус миграции

**Миграция выполнена.** FreeRTOS CMSIS_V2 интегрирован, pipeline разделён
на два task'а (task_producer + task_sd). Smoke test на `demo_stress_64u16.ini`
(64 U16 × 16 CAN IDs × 250 Hz) подтвердил:

- `frames_effective_rate` ≈ 100% (4083 fps при ожидаемых 4000)
- GC stall `sdw max_lat = 389 ms` присутствует, но **не блокирует CAN drain**
- `rb count=0` — ring buffer полностью дренируется даже во время stall
- `err=0/0` — нет ошибок записи

### Что реализовано

- CubeMX: FreeRTOS CMSIS_V2, TIM6 timebase, heap_4 (16 KB)
- `task_producer` (osPriorityNormal): CAN drain → can_map_process → shadow
  update под osMutex, demo gen, USB CDC CLI, LED. `osDelay(1)` yield.
- `task_sd` (osPriorityBelowNormal): `osDelayUntil` periodic snapshot
  shadow → `lw_write_snapshot` → io_buf → f_write/f_sync/rotate.
  Блокируется внутри SD_write при GC stall — только этот task стоит.
- `SD_status` retry: `HAL_Delay(1)` → `osDelay(1)` (ключевая точка yield)
- `SD_write`: `WriteStatus` polling → `osMessageQueueGet` (RTOS DMA
  completion) + `osDelay(1)` в card-state-wait loop
- `_FS_REENTRANT = 1` (CubeMX auto) — FatFS thread-safe через osSemaphore,
  `cmd_get`/`cmd_ls`/`lw_pause` из task_producer безопасно конкурируют с task_sd
- Handoff model: snapshot (rusEFI outputChannels pattern), без очереди

### Ожидает

- Long-term stress test на `demo_stress_64u16.ini` (≥ 2 ч)
- Max stress test `demo_stress_128u16.ini` (128 U16 × 1 kHz) — см.
  `STRESS_TEST_128U16_PLAN.md`

## Историческая позиция (до миграции)

- **Problem is real**: 12% data loss на 2-часовом треке — неприемлемо
- **Fix выбран**: миграция на FreeRTOS с выделенным SD writer task
- **Референс**: rusEFI mmc_card.cpp / thread_priority.h — точно такая
  же архитектура, обкатана годами на той же STM32F4 + SDIO DMA
- **Roadmap**: см. `../REQUIREMENTS.md` → v1.0 → "SD writer decoupling"
- **Тесты до/после**: см. `STRESS_TEST_128U16_PLAN.md` — финальный
  max stress test после миграции, baseline сейчас и сравнение потом

## References

- rusEFI `firmware/hw_layer/mmc_card.cpp` — MMCmonThread, 908-964
- rusEFI `firmware/controllers/system/buffered_writer.h` — BufferedWriter<512>
- rusEFI `firmware/controllers/thread_priority.h` — PRIO_MMC = NORMALPRIO-1
- rusEFI `firmware/hw_layer/ports/rusefi_halconf.h:46` — SDC_NICE_WAITING = TRUE
- ChibiOS `os/hal/src/hal_sdc.c` — sdcWrite / PROGRAMMING poll loop
- STM32CubeMX User Manual — FreeRTOS CMSIS_V2 configuration
- FatFS `ffconf.h` — FF_FS_REENTRANT
- `SD_ERRORS.md` — история SD GC stall исследования
- `CMD_RSP_TIMEOUT.md` — CMD12 и busy-state polling details
