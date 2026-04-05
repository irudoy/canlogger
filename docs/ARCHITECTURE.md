# Архитектура CAN Logger

## Принципы

1. **Testability-first** — максимум логики в `Lib/` (host-testable, нативный gcc). HAL-зависимый код в `Src/` — тонкие обёртки без бизнес-логики.
2. **Dependency inversion** — модули в `Lib/` не знают о HAL. Общение через чистые данные: структуры, буферы, коллбэки.
3. **Данные как контракт** — модули связаны через определённые структуры данных, а не через вызовы друг друга напрямую.

## Слои

```
┌─────────────────────────────────────────────────────────────┐
│                      Application                            │
│                        main.c                               │
│              Инициализация, main loop, glue code            │
├─────────────────────────────────────────────────────────────┤
│                       Lib/ (host-testable)                  │
│                                                             │
│  ┌──────────┐  ┌──────────┐  ┌──────────┐  ┌────────────┐   │
│  │ config   │  │ can_map  │  │  mlvlg   │  │ ring_buf   │   │
│  │ parser   │  │ mapper   │  │ encoder  │  │            │   │
│  └──────────┘  └──────────┘  └──────────┘  └────────────┘   │
├─────────────────────────────────────────────────────────────┤
│                    Src/ (HAL wrappers)                      │
│                                                             │
│  ┌──────────┐  ┌──────────┐  ┌──────────┐  ┌────────────┐   │
│  │ can_drv  │  │ sd_write │  │   led    │  │   button   │   │
│  └──────────┘  └──────────┘  └──────────┘  └────────────┘   │
├─────────────────────────────────────────────────────────────┤
│                    Platform (CubeMX)                        │
│         HAL, CMSIS, FatFS, Drivers, Startup                 │
└─────────────────────────────────────────────────────────────┘
```

## Модули Lib/ (чистая логика)

### config — парсер конфигурации

Читает INI-подобный текст из буфера (не с файловой системы) и заполняет массив структур.

```c
// Lib/config.h

#define CFG_MAX_FIELDS 32
#define CFG_NAME_SIZE  34
#define CFG_UNITS_SIZE 10
#define CFG_CAT_SIZE   34

typedef struct {
  uint32_t can_id;        // CAN ID для прослушивания
  uint8_t  start_byte;    // начальный байт в CAN data (0-7)
  uint8_t  start_bit;     // начальный бит внутри байта (0-7), 0 = весь байт
  uint8_t  bit_length;    // длина в битах (8, 16, 32 и т.д.)
  uint8_t  is_big_endian; // порядок байт в CAN-фрейме
  float    scale;
  float    offset;
  uint8_t  type;          // mlg_FieldType (U08, U16, F32...)
  uint8_t  display_style;
  int8_t   digits;
  char     name[CFG_NAME_SIZE];
  char     units[CFG_UNITS_SIZE];
  char     category[CFG_CAT_SIZE];
} cfg_Field;

typedef struct {
  uint32_t  log_interval_ms;  // интервал записи data block (напр. 10)
  cfg_Field fields[CFG_MAX_FIELDS];
  uint16_t  num_fields;
} cfg_Config;

// Парсит INI-текст из буфера. Возвращает 0 при успехе, код ошибки иначе.
int cfg_parse(const char* text, size_t len, cfg_Config* out);

// Валидирует заполненный конфиг (типы, диапазоны, дубликаты).
int cfg_validate(const cfg_Config* cfg);
```

**Тестируемость:** принимает `const char*` буфер — не зависит от FS. Легко тестировать с произвольными строками.

### can_map — маппинг CAN → значения полей

Извлекает значения из CAN-фрейма по описанию из конфига.

```c
// Lib/can_map.h

// CAN-фрейм (HAL-independent представление)
typedef struct {
  uint32_t id;
  uint8_t  data[8];
  uint8_t  dlc;
} can_Frame;

// Максимальный размер record (CFG_MAX_FIELDS * 8 байт для S64 = 256)
#define CAN_MAP_MAX_RECORD_SIZE 256

// Хранит последние значения всех полей (shadow buffer)
typedef struct {
  uint8_t  values[CAN_MAP_MAX_RECORD_SIZE]; // статический буфер, big-endian
  size_t   record_length;
  uint16_t num_fields;
  uint8_t  updated;      // флаг: были ли обновления с последнего сброса
} can_FieldValues;

// Инициализирует shadow buffer по конфигу. Вычисляет record_length, обнуляет values.
int can_map_init(can_FieldValues* fv, const cfg_Config* cfg);

// Обрабатывает CAN-фрейм: извлекает значения и обновляет shadow buffer.
// Возвращает количество обновлённых полей.
int can_map_process(can_FieldValues* fv, const cfg_Config* cfg, const can_Frame* frame);

// Сбрасывает флаг updated.
void can_map_reset_updated(can_FieldValues* fv);
```

**Тестируемость:** работает с `can_Frame` (простая структура), не с `CAN_RxHeaderTypeDef`. Конверсия HAL → `can_Frame` происходит в `Src/can_drv.c`.

### mlvlg — MLVLG v2 encoder

Уже реализован. Сериализует header, field descriptors, data blocks, markers в буферы.

```c
// Lib/mlvlg.h (существующий API)
int mlg_write_header(uint8_t* buf, size_t buf_size, const mlg_Header* header);
int mlg_write_field(uint8_t* buf, size_t buf_size, const mlg_Field* field);
int mlg_write_data_block(uint8_t* buf, size_t buf_size,
                         uint8_t counter, uint16_t timestamp_10us,
                         const uint8_t* data, size_t data_len);
int mlg_write_marker(uint8_t* buf, size_t buf_size,
                     uint8_t counter, uint16_t timestamp,
                     const char* message);
```

### ring_buf — кольцевой буфер CAN-фреймов

Lock-free SPSC (single-producer single-consumer) для передачи фреймов из ISR в main loop.

```c
// Lib/ring_buf.h

#define RING_BUF_SIZE 64  // должен быть степенью двойки

typedef struct {
  can_Frame frames[RING_BUF_SIZE];
  volatile uint32_t head;  // пишет ISR
  volatile uint32_t tail;  // читает main loop
} ring_Buffer;

void ring_buf_init(ring_Buffer* rb);
int  ring_buf_push(ring_Buffer* rb, const can_Frame* frame);  // из ISR
int  ring_buf_pop(ring_Buffer* rb, can_Frame* frame);          // из main loop
int  ring_buf_is_empty(const ring_Buffer* rb);
int  ring_buf_is_full(const ring_Buffer* rb);
```

**Тестируемость:** чистые данные, volatile для корректности, но тестируется однопоточно на хосте.

## Модули Src/ (HAL-обёртки)

### can_drv — CAN драйвер

Тонкая обёртка над HAL_CAN. Конвертирует HAL-типы в `can_Frame` и кладёт в ring buffer.

```c
// Src/can_drv.c
// HAL_CAN_RxFifo0MsgPendingCallback → конвертация → ring_buf_push()
```

### sd_write — запись на SD

Управляет FatFS: монтирование, открытие файлов, запись буферов, ротация, закрытие.

```c
// Src/sd_write.c
int  sd_init(void);
int  sd_write(const uint8_t* buf, size_t len);
int  sd_read_file(const char* name, char* buf, size_t buf_size, size_t* bytes_read);
void sd_close(void);
int  sd_new_file(const char* name);
```

### led — индикация

```c
// Src/led.c
void led_set_state(int state);  // LED_LOGGING, LED_ERROR, LED_STOPPED, LED_NO_CONFIG
```

## Поток данных

```
                    ┌──────────────────┐
                    │  SD: config.ini  │
                    └────────┬─────────┘
                             │ sd_read_file() → buf
                             ▼
                    ┌──────────────────┐
                    │  cfg_parse(buf)  │  Lib/config
                    └────────┬─────────┘
                             │ cfg_Config
                    ┌────────▼─────────┐
                    │  can_map_init()  │  Lib/can_map
                    │  mlg header +    │  Lib/mlvlg
                    │  fields → SD     │
                    └────────┬─────────┘
                             │
          ┌──────────────────▼──────────────────┐
          │            Main Loop                 │
          │                                      │
          │  ┌─────────────────────────────┐     │
          │  │ while ring_buf_pop(frame):  │     │
          │  │   can_map_process(frame)    │     │
          │  └─────────────────────────────┘     │
          │                                      │
          │  ┌─────────────────────────────┐     │
          │  │ if timer >= log_interval:   │     │
          │  │   mlg_write_data_block(     │     │
          │  │     shadow_values)          │     │
          │  │   sd_write(block_buf)       │     │
          │  └─────────────────────────────┘     │
          └──────────────────────────────────────┘
                             ▲
                             │ ISR (CAN RX)
          ┌──────────────────┴──────────────────┐
          │  HAL_CAN_RxFifo0MsgPendingCallback  │
          │    → can_Frame → ring_buf_push()    │
          └─────────────────────────────────────┘
```

## Формат конфиг-файла (config.ini)

```ini
[logger]
interval_ms = 10
can_bitrate = 500000       # 125000, 250000, 500000, 1000000 (default 500000)

[field]
can_id = 0x666
name = Coolant Temp
units = C
start_byte = 1
bit_length = 8
type = U08
scale = 1.0
offset = -40.0
digits = 0
category = Engine

[field]
can_id = 0x667
name = RPM
units = rpm
start_byte = 1
bit_length = 16
type = U16
scale = 12.5
offset = 0.0
is_big_endian = 1
digits = 0
category = Engine
```

### Секция `[logger]`

| Ключ | Обязательный | Default | Описание |
|------|-------------|---------|----------|
| `interval_ms` | да | — | Интервал записи data block (мс) |
| `can_bitrate` | нет | 500000 | Скорость CAN шины (125000/250000/500000/1000000) |

### Секции `[field]`

| Ключ | Обязательный | Default | Описание |
|------|-------------|---------|----------|
| `can_id` | да | — | CAN ID (hex 0x... или decimal) |
| `name` | да | — | Имя параметра (до 33 символов) |
| `units` | да | — | Единицы измерения (до 9 символов) |
| `start_byte` | да | — | Начальный байт в CAN data (0-7) |
| `bit_length` | да | — | Длина данных в битах (8, 16, 32, 64) |
| `type` | да | — | Тип: U08, S08, U16, S16, U32, S32, S64, F32 |
| `scale` | да | — | Множитель для отображения |
| `offset` | да | — | Смещение для отображения |
| `is_big_endian` | нет | 0 | Порядок байт в CAN фрейме (0=LE, 1=BE) |
| `digits` | нет | 0 | Кол-во знаков после запятой |
| `display_style` | нет | 0 | Стиль отображения (0=Float) |
| `category` | нет | "" | Категория для группировки в MLV |
| `lut` | нет | — | Lookup table для нелинейной конверсии (см. ниже) |

- Порядок секций `[field]` определяет порядок полей в MLG
- Несколько полей могут ссылаться на один `can_id`
- Уникальные CAN ID автоматически собираются для настройки hardware фильтров
- Валидация: `bit_length` кратен 8, `start_byte + bit_length/8 <= 8`

### Lookup Table (LUT)

Для нелинейных сенсоров (NTC, нелинейные датчики давления) можно задать кусочно-линейную таблицу преобразования:

```ini
lut = input1:output1, input2:output2, ...
```

- `input` — сырое значение из CAN (uint16, например мВ)
- `output` — значение в единицах отображения (int16, например °C или kPa)
- Минимум 2 точки, максимум 16
- Точки должны быть отсортированы по возрастанию input
- Линейная интерполяция между точками, clamp за пределами
- При наличии LUT, `scale`/`offset` используются для хранения в MLG (stored = display / scale - offset)

Пример — NTC термистор (Bosch 0261230042, pull-up 2.2k):

```ini
[field]
can_id = 0x640
name = IAT
units = C
start_byte = 0
bit_length = 16
type = S16
scale = 0.1
offset = 0.0
is_big_endian = 1
digits = 0
category = SwitchBoard
lut = 192:120, 325:100, 428:90, 570:80, 760:70, 1014:60, 1346:50, 1765:40, 2259:30, 2807:20, 3353:10, 3848:0, 4543:-20, 4860:-40
```

Пример — линейный MAP (2 точки из даташита):

```ini
[field]
can_id = 0x640
name = MAP
units = kPa
start_byte = 2
bit_length = 16
type = U16
scale = 1.0
offset = 0.0
is_big_endian = 1
digits = 0
category = SwitchBoard
lut = 400:20, 4650:250
```

## Жизненный цикл (main.c)

### Инициализация

```
1. HAL_Init(), SystemClock_Config()
2. GPIO, SDIO, RTC init (CubeMX)
3. sd_init() — монтирование SD
4. sd_read_file("config.ini") → буфер
5. cfg_parse(буфер) → cfg_Config
6.   если нет конфига или ошибка парсинга → led_set_state(LED_NO_CONFIG), стоп
7. can_map_init(cfg) → can_FieldValues (shadow buffer)
8. Запись MLG header + field descriptors на SD
9. CAN init (CubeMX HAL), запуск приёма
10. led_set_state(LED_LOGGING)
```

### Main loop

```
while (!shutdown) {
    // 1. Вычитать все CAN-фреймы из ring buffer
    while (ring_buf_pop(&rb, &frame))
        can_map_process(&fv, &cfg, &frame);

    // 2. По таймеру — записать data block
    if (HAL_GetTick() - last_log >= cfg.log_interval_ms) {
        mlg_write_data_block(buf, ..., fv.values, fv.record_length);
        sd_write(buf, block_size);
        last_log = HAL_GetTick();
    }

    // 3. Ротация файла при превышении размера
    // 4. LED индикация
}
```

### Shutdown (по кнопке K1 или потере питания)

```
1. Дописать текущий буфер на SD
2. sd_close()
3. led_set_state(LED_STOPPED)
```

## Обработка ошибок

| Ситуация | Поведение |
|----------|-----------|
| Нет SD карты | `sd_init()` fail → LED_ERROR, не стартуем |
| Нет config.ini | `sd_read_file()` fail → LED_NO_CONFIG, не стартуем |
| Невалидный конфиг | `cfg_parse()`/`cfg_validate()` fail → LED_NO_CONFIG, не стартуем |
| Ошибка записи SD | Retry до N раз, затем LED_ERROR, прекращаем запись |
| Ring buffer переполнен | `ring_buf_push()` возвращает ошибку, фрейм теряется (счётчик потерь) |
| Файл достиг 4MB | Закрыть текущий, открыть новый (ротация) |

## Стратегия тестирования

### Unit-тесты (Lib/, каждый модуль изолированно)

| Модуль | Что тестируем |
|--------|---------------|
| config | Парсинг валидного INI, ошибки синтаксиса, граничные значения, невалидные типы |
| can_map | Извлечение U08/U16/U32/F32 из CAN data, big/little endian, scale/offset, мульти-ID |
| mlvlg | Формат header/field/data block/marker, endianness, CRC, buffer overflow |
| ring_buf | Push/pop, пустой/полный, wraparound, порядок FIFO |

### Snapshot-тесты (промежуточные структуры)

Эталонные бинарные буферы для:
- Сериализованный MLG header (24 байта) для заданного набора полей
- Сериализованный field descriptor (89 байт) для конкретного поля
- Сериализованный data block с известными входными значениями

Тест побайтово сравнивает выход с зафиксированным эталоном. При изменении формата — эталон обновляется явно.

### Snapshot-тесты (полный файл)

- Собрать полный `.mlg` файл из конфига + набора CAN-фреймов
- Сравнить побайтово с эталонным `.mlg`
- Дополнительно: прогнать через `mlg-converter` (Node.js) и проверить что парсинг успешен

### Интеграционные тесты на хосте

Полный пайплайн без HAL:
1. `cfg_parse(ini_text)` → `cfg_Config`
2. `can_map_init(cfg)` → `can_FieldValues`
3. Серия `can_map_process(frame)` с тестовыми фреймами
4. `mlg_write_header()` + `mlg_write_field()` + `mlg_write_data_block(shadow_values)`
5. Записать результат в файл
6. Валидация: `node mlg-test/validate.js output.mlg` → парсинг + проверка значений

### E2E тест (на железе)

cansult (CAN source) → canlogger → SD → ПК → MegaLogViewer

## Структура файлов (целевая)

```
firmware/
├── Lib/                    # Чистая логика (host-testable)
│   ├── mlvlg.{h,c}        # MLVLG v2 encoder
│   ├── config.{h,c}       # INI-парсер конфигурации
│   ├── can_map.{h,c}      # CAN → field values маппинг
│   └── ring_buf.{h,c}     # Lock-free SPSC ring buffer
├── Src/                    # HAL-зависимые обёртки
│   ├── main.c              # Init, main loop, glue
│   ├── can_drv.c           # CAN HAL → ring_buf
│   ├── sd_write.c          # FatFS обёртка
│   ├── led.c               # LED индикация
│   └── [CubeMX files]
├── Inc/                    # Headers для Src/
│   ├── can_drv.h
│   ├── sd_write.h
│   ├── led.h
│   └── [CubeMX headers]
├── test/                   # Host-тесты
│   ├── unity/
│   ├── test_mlvlg.c
│   ├── test_config.c
│   ├── test_can_map.c
│   ├── test_ring_buf.c
│   ├── test_integration.c
│   ├── snapshots/          # Эталонные бинарные файлы
│   └── Makefile
└── Makefile                # build, test, flash, debug
```
