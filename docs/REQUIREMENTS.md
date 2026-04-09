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
- [ ] RTC от LSE (32.768 kHz) вместо LSI — точное время в логах
- [ ] Поддержка extended CAN ID (29-bit) в конфиге и can_map
- [ ] Индикация состояния через LED (запись, ошибка, нет конфига, нет SD)
- [ ] Обработка ошибок SD (FR_DISK_ERR@write после ~20 мин, см. [SD_ERRORS.md](SD_ERRORS.md), [CMD_RSP_TIMEOUT.md](CMD_RSP_TIMEOUT.md)):
  - [ ] Проверять результат f_sync (сейчас игнорируется)
  - [ ] Recovery вместо fatal: close → remount → new file → продолжить запись
  - [ ] Уменьшить интервал f_sync (100 → 10 блоков)
  - [ ] Заменить HAL_Delay(1000) в recover_file() на non-blocking recovery (таймер + state machine)
  - [ ] Счётчик recovery в статусе (rec=N)
  - [ ] Исследовать: SDIO clock divider, питание SD, другая карта
  - [ ] Убрать дублирование mlg_fields[256] (23KB RAM) — строить MLG header на лету из cfg_Config
  - [ ] recover_file() блокирует main loop на 30с при вытащенной SD (f_mount → SD_CheckStatusWithTimeout)
  - [ ] Stress test max pressure: 128 полей / 32 CAN ID / 4ms (65 KB/s) — CMD_RSP_TIMEOUT через ~48с.
    Карта (SanDisk Ultra 32GB) уходит в busy (GC), перестаёт отвечать на SDIO команды.
    hsd.ErrorCode=4 (CTIMEOUT), sd_ErrorCounters все нули (timeout в polling, не ISR).
    Recovery помогает временно (5 успешных), потом тоже падает.
    f_expand(32MB, opt=0) медленный — блокирует main loop при каждом recovery/rotation.
    Half-pressure (64 поля / 16 CAN ID / 4ms, ~32 KB/s) — тоже падает.
    Даже cansult 13 полей падает через ~80с на чистой карте без f_expand (стек 12KB).
    Проблема не в нагрузке — нужно исследовать DMA write path (sd_write_dma.c),
    SDIO DTIMER, сравнить с commit до demo-through-ringbuf изменений.
- [ ] Отладочный лог на SD — системные события, ошибки, сэмплы данных по условию
- [ ] Circular logging — при заполнении SD удалять самые старые MLG файлы и продолжать запись
- [ ] Логирование статистики (принято/потеряно/записано фреймов)
- [x] DMA для SDIO (reorder + PBURST_INC4, TX_UNDERRUN исправлен)
- [x] Исследовать CMD_RSP_TIMEOUT при DMA записи — см. [CMD_RSP_TIMEOUT.md](CMD_RSP_TIMEOUT.md)
  - [x] Анализ: вероятно CMD12 stop при busy карте, см. docs/CMD_RSP_TIMEOUT.md
  - [x] SDIO error counters через HAL_SD_ErrorCallback + hal.ErrorCode в CDC status
  - [x] FAULT file на SD при фатальной ошибке (FAULT_NN.TXT с полной диагностикой)
- [ ] Поддержка фильтрации CAN ID на аппаратном уровне (HAL CAN filter banks)
- [ ] Валидация конфига при загрузке с диагностикой ошибок
- [ ] Настройка max_file_size через config.ini (сейчас хардкод 512 МБ)
- [ ] GPS модуль — геопозиция + точное реальное время
- [ ] Поле "Date" в MLG (U32 unix timestamp, display_style=MLG_DATE) — реальное время на таймлайне MegaLogViewer
- [ ] Полнота реализации MLG — изучить спеку, проверить все ли возможности используются

### v2.0 — Удобство конфигурации

**Цель:** Простое создание конфигов без ручного редактирования.

Задачи:
- [ ] Web-UI для генерации конфиг-файла (выбор CAN ID, настройка параметров)
- [ ] Режим сниффера: запись raw CAN-трафика для анализа и создания маппинга
- [ ] Импорт DBC-файлов (стандарт описания CAN-базы данных) → авто-генерация конфига
- [x] USB-интерфейс для выгрузки логов (`get`) и загрузки конфигов (`put`) без извлечения SD

## Ограничения и допущения

- Целевая частота записи — максимально возможная на текущем железе
- STM32F407 имеет 2 CAN-контроллера (CAN1, CAN2) — можно поддержать оба
- SDIO в 1-bit mode — можно переключить на 4-bit для скорости
- Формат конфига на первом этапе может быть простым (INI/CSV), оптимизируется позже
- Логгер не передаёт данные — только пишет на SD
