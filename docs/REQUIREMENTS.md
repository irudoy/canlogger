# Требования и роадмап: CAN Logger

## Видение проекта

Автономный универсальный логгер параметров CAN-шины, записывающий данные на SD-карту в формате MLVLG v2 (совместим с MegaLogViewer).

**Ключевое свойство — универсальность.** Логгер не знает заранее, какие CAN-сообщения и параметры он будет записывать. Конфигурация маппинга CAN → MLG задаётся файлом на SD-карте. Это позволяет использовать одно устройство с разными автомобилями и протоколами без перепрошивки.

## Аппаратная архитектура

См. [HARDWARE.md](HARDWARE.md) — плата, пины, подключения, hat, E2E стенд.

## Программная архитектура

См. [ARCHITECTURE.md](ARCHITECTURE.md) — модули, потоки данных, формат конфига, стратегия тестирования.

## Роадмап

### PoC — done

**Цель:** Доказать концепцию: записать валидный MLVLG-файл с тестовыми данными и открыть его в MegaLogViewer.

Подход: TDD — сначала тесты на хосте, потом проверка на устройстве.

Результат:
- [x] Очистить legacy код (убрать `Src/mlvlg.c`, `Inc/mlvlg.h` — заменены на `Lib/mlvlg.*`)
- [x] Snapshot-тест: сгенерировать полный .mlg файл на хосте, валидировать через `mlg-converter`
- [x] Интегрировать `Lib/mlvlg` с SD записью: header + фиксированные поля + тестовые data blocks
- [x] Записать тестовые данные (счётчик / синусоида) в MLVLG файл на устройстве
- [x] Открыть файл в MegaLogViewer и проверить корректность (параметры отображаются, графики работают)

21 тест (unit + snapshot), 0 failures. 453 записи при 20Hz, валидировано mlg-converter и MegaLogViewer.

### MVP — Базовый рабочий логгер — done

**Цель:** Записать CAN-данные с cansult на SD в валидный MLVLG-файл, открываемый в MegaLogViewer.

Результат:
- [x] Включить CAN-периферию в CubeMX (CAN1 PB8/PB9, 500 kbit/s)
- [x] Реализовать `can_drv` — CAN RX через прерывания → ring buffer (`Lib/ring_buf`)
- [x] Реализовать `Lib/config` — INI-парсер конфигурации с SD
- [x] Реализовать `Lib/can_map` — маппинг CAN-фреймов → MLG field values по конфигу
- [x] Реализовать config-driven запись MLG header + data blocks на SD (в `log_writer`)
- [x] Интеграция в main loop: config → can_map → mlvlg → sd_write
- [x] Поведение без конфига: ошибка + LED индикация + K1 shutdown
- [x] Ручной конфиг для cansult (11 параметров) — `test/cansult_config.ini`
- [x] Тесты: парсер конфига, маппинг, bit extraction, ring buffer (58 тестов)
- [x] E2E тест: cansult → Nissan ECU → CAN → canlogger → SD → MegaLogViewer
- [x] Проверка в MegaLogViewer: Battery voltage graph 11.4V–15.5V с реальными изменениями

Конфигурируемый CAN bitrate, hardware ID фильтры, unix timestamp в MLG header.

### Следующий этап — отладка и стабильность

- [x] Debug-обмен данными без извлечения SD → **USB CDC** (Virtual COM Port)
- [x] Исследовать и исправить отвал cansult UART↔ECU через время
- [x] Документация по конфигурации и формату MLG для пользователей
- [x] Улучшение debug-интерфейса: CLI через USB CDC (help/status/stream/config/ls/get), скачивание MLG файлов (usb_get.py)

### v1.0 — Стабильный продукт

**Цель:** Надёжный логгер для повседневного использования.

Задачи:
- [ ] Graceful shutdown при пропадании питания (обнаружение + запись буфера + f_close)
- [ ] Hat PCB: CAN-трансивер + DC-DC + shutdown circuit
- [ ] Поддержка extended CAN ID (29-bit) в конфиге и can_map
- [ ] Circular logging — при заполнении SD удалять самые старые MLG файлы и продолжать запись
- [ ] Валидация конфига при загрузке с диагностикой ошибок
- [ ] Поле "Date" в MLG (U32 unix timestamp, display_style=MLG_DATE) — реальное время на таймлайне MegaLogViewer
- [ ] Настройка max_file_size через config.ini (сейчас хардкод 512 МБ)
- [ ] Отладочный лог на SD — системные события, ошибки, сэмплы данных по условию
- [ ] Логирование статистики (принято/потеряно/записано фреймов)
- [ ] Поддержка фильтрации CAN ID на аппаратном уровне (HAL CAN filter banks)
- [ ] Поддержка двух CAN-шин (CAN1 + CAN2) — два независимных канала, удвоение пропускной способности
- [ ] GPS модуль — геопозиция + точное реальное время
- [ ] Полнота реализации MLG — изучить спеку, проверить все ли возможности используются
- [x] RTC от LSE (32.768 kHz) вместо LSI — точное время в логах, VBAT батарейка на плате
- [x] Индикация состояния через LED (запись, ошибка, нет конфига, нет SD)
- [x] DMA для SDIO (reorder + PBURST_INC4, TX_UNDERRUN исправлен)
- [x] Обработка ошибок SD (FR_DISK_ERR@write после ~20 мин, см. [SD_ERRORS.md](SD_ERRORS.md), [CMD_RSP_TIMEOUT.md](CMD_RSP_TIMEOUT.md)):
  - [x] Проверять результат f_sync — recovery при ошибке
  - [x] Recovery вместо fatal: close → remount → new file → продолжить запись
  - [~] ~~Уменьшить интервал f_sync (100 → 10 блоков)~~ — не нужно: supercap + graceful shutdown гарантируют flush при выключении, а частый f_sync увеличивает шанс GC stall
  - [x] Заменить HAL_Delay(1000) в recover_file() на non-blocking — osDelay (FreeRTOS, блокирует только task_sd)
  - [x] Счётчик recovery в статусе (rec=N lastrec=FR_X@site)
  - [x] Убрать дублирование mlg_fields[256] (23KB RAM) — строить MLG header на лету из cfg_Config
  - [x] recover_file() блокировала main loop на 30с — теперь блокирует только task_sd (osDelay), CAN drain продолжается
  - [x] Stress test max pressure: 128 полей / 32 CAN ID / 1ms — решено миграцией на FreeRTOS (GC stalls блокируют только task_sd)
- [x] Исследовать CMD_RSP_TIMEOUT при DMA записи — см. [CMD_RSP_TIMEOUT.md](CMD_RSP_TIMEOUT.md)
  - [x] Анализ: вероятно CMD12 stop при busy карте, см. docs/CMD_RSP_TIMEOUT.md
  - [x] SDIO error counters через HAL_SD_ErrorCallback + hal.ErrorCode в CDC status
  - [x] FAULT file на SD при фатальной ошибке (FAULT_NN.TXT с полной диагностикой)
- [x] **SD writer decoupling — миграция на FreeRTOS**
      (см. [SD_WRITER_DECOUPLING.md](SD_WRITER_DECOUPLING.md)):
  - корневая причина: blocking `SD_write` в main loop → GC stalls до 710 мс
    останавливают весь pipeline → **~12% потеря sample** на 2-часовом тесте
    (64 U16 @ 250 Hz: expected 4000 fps, actual 3509 fps)
  - на max-нагрузке 128 U16 @ 1 kHz потеря пропорционально растянется
  - референс: rusEFI `mmc_card.cpp` — выделенный SD thread с приоритетом
    `NORMALPRIO-1`, `SDC_NICE_WAITING=TRUE`, main loop не блокируется
  - план: CubeMX → Middleware → FREERTOS CMSIS_V2 → выделить
    `task_sd` (osPriorityBelowNormal) с очередью MLG records;
    `task_log` (osPriorityNormal) собирает snapshot → queue;
    CAN RX + can_map в высокоприоритетном task
  - FatFS должен быть reentrant (`FF_FS_REENTRANT=1`)
  - `SD_status` retry: `HAL_Delay` → `osDelay` (yield-friendly)
  - финальная проверка: `docs/STRESS_TEST_128U16_PLAN.md`
- [x] `RING_BUF_SIZE` 1024 → 4096 (64 KB, покрывает 225 мс @ 18k fps):
  - разблокировано оптимизацией RAM (config → CCM, mlg_fields убран)
  - комфорт: 8192 (128 KB, 450 мс) — возможен при переносе `can_rx_buf` в CCM (сейчас не помещается)
- [x] Оптимизация RAM (main SRAM: 54 KB из 128 KB занято, CCM: 53 KB из 64 KB):
  - Убран `mlg_fields[256]` (23 KB) — MLG header строится на лету из `cfg_Config`
  - `config` (52.7 KB) перенесён в CCM SRAM (`.ccmram` section)
  - FreeRTOS heap 8 → 16 KB, sdTask stack 2 → 4 KB
  - `CAN_SNIFF_MAX` 16 → 32 (полное покрытие CAN ID в status)

### v2.0 — Удобство конфигурации

**Цель:** Простое создание конфигов без ручного редактирования.

Задачи:
- [ ] Web-UI для генерации конфиг-файла (выбор CAN ID, настройка параметров)
- [ ] Режим сниффера: запись raw CAN-трафика для анализа и создания маппинга
- [ ] USB-интерфейс для выгрузки логов (`get`) и загрузки конфигов (`put`) без извлечения SD (частично реализовано для отладки, но нет пользовательского интерфейса)
- [ ] WIFI ESP Module

## Ограничения и допущения

- Целевая частота записи — максимально возможная на текущем железе
- STM32F407 имеет 2 CAN-контроллера (CAN1, CAN2) — можно поддержать оба
- Формат конфига на первом этапе может быть простым (INI/CSV), оптимизируется позже
- Логгер не передаёт данные — только пишет на SD
