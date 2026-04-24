# CAN Logger architecture

## Principles

1. **Testability-first** — maximise logic that lives in `Lib/` (host-testable, native gcc). HAL-dependent code in `Src/` stays a thin wrapper with no business logic.
2. **Dependency inversion** — modules in `Lib/` know nothing about HAL. They communicate through plain data: structs, buffers, callbacks.
3. **Data as contract** — modules are wired together through well-defined data structures, not direct function calls between each other.

## Layers

```
┌─────────────────────────────────────────────────────────────┐
│                      Application                            │
│                        main.c                               │
│          Init, FreeRTOS tasks, glue code                    │
├─────────────────────────────────────────────────────────────┤
│                       Lib/ (host-testable)                  │
│                                                             │
│  ┌──────────┐  ┌──────────┐  ┌──────────┐  ┌────────────┐   │
│  │ config   │  │ can_map  │  │  mlvlg   │  │ ring_buf   │   │
│  │ parser   │  │ mapper   │  │ encoder  │  │            │   │
│  └──────────┘  └──────────┘  └──────────┘  └────────────┘   │
│  ┌──────────┐                                               │
│  │ demo_gen │                                               │
│  └──────────┘                                               │
├─────────────────────────────────────────────────────────────┤
│                    Src/ (HAL wrappers)                      │
│                                                             │
│  ┌──────────┐  ┌──────────┐  ┌──────────┐  ┌────────────┐   │
│  │ can_drv  │  │ sd_write │  │   led    │  │   button   │   │
│  └──────────┘  └──────────┘  └──────────┘  └────────────┘   │
├─────────────────────────────────────────────────────────────┤
│                    Platform (CubeMX)                        │
│      HAL, CMSIS, FatFS, FreeRTOS CMSIS_V2, Drivers          │
└─────────────────────────────────────────────────────────────┘
```

## Lib/ modules (pure logic)

### config — configuration parser

Reads INI-like text from a buffer (not from the filesystem) and fills a struct array.

```c
// Lib/config.h (CFG_MAX_FIELDS, CFG_MAX_CAN_IDS — see cfg_limits.h)

#define CFG_NAME_SIZE  34
#define CFG_UNITS_SIZE 10
#define CFG_CAT_SIZE   34

typedef struct {
  uint32_t can_id;        // CAN ID to listen for
  uint8_t  is_extended;   // 1 = 29-bit extended ID (AEMnet/J1939), 0 = 11-bit standard
  uint8_t  start_byte;    // starting byte in CAN data (0-7)
  uint8_t  start_bit;     // starting bit within the byte (0-7); only for bit_length<8
  uint8_t  bit_length;    // length in bits: 1-7 (sub-byte), 8, 16, 32, 64
  uint8_t  is_big_endian; // byte order inside the CAN frame (ignored for sub-byte)
  float    scale;
  float    offset;
  uint8_t  type;          // mlg_FieldType (U08, U16, F32...)
  uint8_t  display_style;
  int8_t   digits;
  char     name[CFG_NAME_SIZE];
  char     units[CFG_UNITS_SIZE];
  char     category[CFG_CAT_SIZE];
  cfg_LutPoint lut[CFG_LUT_MAX];
  uint8_t  lut_count;
  float    valid_min, valid_max;       // display-unit bounds (optional)
  uint8_t  has_valid_min, has_valid_max;
  uint8_t  invalid_strategy;           // CFG_INVALID_LAST_GOOD|CLAMP|SKIP
  uint8_t  preset;                     // CFG_PRESET_NONE|AEM_UEGO
  uint8_t  gps_source;                 // CFG_GPS_SRC_* (0 = field is not GPS-sourced)
} cfg_Field;

typedef struct {
  uint32_t  log_interval_ms;  // data-block write interval (e.g. 10)
  uint32_t  can_bitrate;      // CAN bus speed (default 500000)
  cfg_Field fields[CFG_MAX_FIELDS];
  uint16_t  num_fields;
  uint32_t  can_ids[CFG_MAX_CAN_IDS];
  uint8_t   can_ids_extended[CFG_MAX_CAN_IDS];  // parallel flag for filter setup
  uint16_t  num_can_ids;
  uint8_t   demo;             // auto: 1 if any field has demo_func
  demo_Gen  demo_gen;
  uint8_t   gps_enabled;      // `[gps] enable = 1` — auto-injects gps_lat/lon/alt/speed_kmh/fix
} cfg_Config;

// Parses INI text from a buffer. Returns 0 on success, error code otherwise.
int cfg_parse(const char* text, size_t len, cfg_Config* out);

// Validates a populated config (types, ranges, duplicates).
int cfg_validate(const cfg_Config* cfg);
```

**Testability:** takes a `const char*` buffer — no filesystem dependency. Easy to test with arbitrary strings.

### can_map — CAN → field-value mapping

Extracts field values from a CAN frame according to the config description.

```c
// Lib/can_map.h

// CAN frame (HAL-independent representation)
typedef struct {
  uint32_t id;
  uint8_t  data[8];
  uint8_t  dlc;
} can_Frame;

// Maximum record size (CFG_MAX_FIELDS * 8 bytes for S64 = 256)
#define CAN_MAP_MAX_RECORD_SIZE 256

// Holds the latest values of every field (shadow buffer)
typedef struct {
  uint8_t  values[CAN_MAP_MAX_RECORD_SIZE]; // static buffer, big-endian
  size_t   record_length;
  uint16_t num_fields;
  uint8_t  updated;      // flag: whether any update has happened since the last reset
} can_FieldValues;

// Initialises the shadow buffer from the config. Computes record_length, zeroes values.
int can_map_init(can_FieldValues* fv, const cfg_Config* cfg);

// Handles a CAN frame: extracts values and updates the shadow buffer.
// Returns the number of updated fields.
int can_map_process(can_FieldValues* fv, const cfg_Config* cfg, const can_Frame* frame);

// Writes into the shadow buffer the values of every field with gps_source != NONE from gps_State.
// Called under shadow_mutex; gps_drv_poll() itself runs outside the mutex so that the
// raw-tap putchar does not stall task_sd.
int can_map_update_gps(can_FieldValues* fv, const cfg_Config* cfg, const gps_State* gs);

// Clears the updated flag.
void can_map_reset_updated(can_FieldValues* fv);
```

**Testability:** operates on `can_Frame` (a simple struct), not `CAN_RxHeaderTypeDef`. The HAL → `can_Frame` conversion happens in `Src/can_drv.c`.

### mlvlg — MLVLG v2 encoder

Already implemented. Serialises the header, field descriptors, data blocks, and markers into buffers.

```c
// Lib/mlvlg.h (existing API)
int mlg_write_header(uint8_t* buf, size_t buf_size, const mlg_Header* header);
int mlg_write_field(uint8_t* buf, size_t buf_size, const mlg_Field* field);
int mlg_write_data_block(uint8_t* buf, size_t buf_size,
                         uint8_t counter, uint16_t timestamp_10us,
                         const uint8_t* data, size_t data_len);
int mlg_write_marker(uint8_t* buf, size_t buf_size,
                     uint8_t counter, uint16_t timestamp,
                     const char* message);
```

### gps_nmea — NMEA-0183 parser

Pure-C line assembler + sentence decoder for the u-blox NEO-6M and compatible receivers. Runs on the host under Unity.

```c
// Lib/gps_nmea.h

// Accumulating state; fields persist across messages, has_* flags indicate
// what actually came in. All known talker IDs are accepted (GP/GN/GL/GA/BD/GB).
typedef struct {
  uint8_t  hour, minute, second; uint16_t millisecond; uint8_t has_time;
  uint16_t year; uint8_t month, day; uint8_t has_date;
  double   lat_deg, lon_deg; uint8_t has_position;
  float    altitude_m;       uint8_t has_altitude;
  uint8_t  fix_quality, satellites; float hdop; uint8_t has_fix;
  float    speed_ms, course_deg; uint8_t has_motion;
} gps_State;

typedef struct {
  char    buf[GPS_NMEA_MAX_LEN];
  uint8_t len, in_sentence, overflow;
} gps_LineBuffer;

int  gps_lb_feed_byte(gps_LineBuffer* lb, uint8_t b, const char** out);
gps_ParseResult gps_parse_sentence(gps_State* s, const char* sentence);
```

Parses GGA (time/fix/coordinates/altitude/satellites/HDOP) and RMC (time/date/coordinates/speed/course). Other NMEA types (VTG/GSA/GSV/GLL) are recognised but return `GPS_PARSE_IGNORED`. `$` always restarts the sentence; on overflow the `overflow` flag is latched and cleared on the next `$`.

### ring_buf — CAN-frame ring buffer

Lock-free SPSC (single-producer, single-consumer) for moving frames from the ISR to task_producer.

```c
// Lib/ring_buf.h

#define RING_BUF_SIZE 4096  // must be a power of two

typedef struct {
  can_Frame frames[RING_BUF_SIZE];
  volatile uint32_t head;  // written by ISR (or demo_pack from task_producer)
  volatile uint32_t tail;  // read by task_producer
} ring_Buffer;

void ring_buf_init(ring_Buffer* rb);
int  ring_buf_push(ring_Buffer* rb, const can_Frame* frame);  // from ISR / demo_pack
int  ring_buf_pop(ring_Buffer* rb, can_Frame* frame);          // from task_producer
int  ring_buf_is_empty(const ring_Buffer* rb);
int  ring_buf_is_full(const ring_Buffer* rb);
```

**Testability:** plain data, volatile for correctness, but tested single-threaded on the host.

## Src/ modules (HAL wrappers)

### can_drv — CAN driver

Thin wrapper over HAL_CAN. Converts HAL types to `can_Frame` and pushes into the ring buffer.

```c
// Src/can_drv.c
// HAL_CAN_RxFifo0MsgPendingCallback → convert → ring_buf_push()
```

### gps_drv — USART3 DMA → NMEA parser

Wrapper over `HAL_UART_Receive_DMA` in CIRCULAR mode. The `rx_dma_buf[128]` buffer lives in main SRAM (DMA1 on the F4 cannot reach CCM). Before starting DMA, `gps_drv_init()` clears ORE (`__HAL_UART_CLEAR_OREFLAG`) — `MX_USART3_UART_Init` turns RE on before DMA starts, and the first few bytes latch the error flag. A periodic poll from `task_producer` (outside `shadow_mutex`) reads `NDTR` to compute `write_pos` and drains the new bytes through `gps_lb_feed_byte` into `gps_nmea`. `gps_drv_state()` returns a snapshot of the current `gps_State`; it is copied into the shadow via `can_map_update_gps()` while holding `shadow_mutex`.

```c
// Src/gps_drv.c
void gps_drv_init(void);             // start circular DMA RX
int  gps_drv_poll(void);             // drain + parse; returns count of OK sentences
const gps_State* gps_drv_state(void);
void gps_drv_set_raw(uint8_t en);    // CDC passthrough for hardware debugging
```

### sd_write — SD writer

Manages FatFS: mount, open file, write buffer, rotate, close.

```c
// Src/sd_write.c
int  sd_init(void);
int  sd_write(const uint8_t* buf, size_t len);
int  sd_read_file(const char* name, char* buf, size_t buf_size, size_t* bytes_read);
void sd_close(void);
int  sd_new_file(const char* name);
```

### led — indication

```c
// Src/led.c
void led_set_state(int state);  // LED_LOGGING, LED_ERROR, LED_STOPPED, LED_NO_CONFIG
```

## Data flow (FreeRTOS, 2 tasks)

Architecture: snapshot model modelled after rusEFI `MMCmonThread`.
Shared `field_values` is the analogue of rusEFI `outputChannels`.
Migration details: `docs/postmortem/SD_WRITER_DECOUPLING.md`.

```
          CAN ISR (NVIC prio 5)
          ┌─────────────────────────────────────┐
          │  HAL_CAN_RxFifo0MsgPendingCallback  │
          │    → can_Frame → ring_buf_push()    │
          └──────────────────┬──────────────────┘
                             │
                             ▼ ring_Buffer (SPSC, 4096 slots)
   ┌─────────────────────────────────────────────────────┐
   │  task_producer  (osPriorityNormal, defaultTask)     │
   │                                                     │
   │  1. Init: lw_init → SD mount, config, MLG header    │
   │  2. Loop (osDelay 1 ms):                            │
   │     ├─ demo_pack_can_frames (if demo mode)          │
   │     ├─ mutex { ring_buf_pop → can_map_process →     │
   │     │          update field_values shadow }          │
   │     ├─ debug_out_tick / debug_cmd_poll (CDC CLI)    │
   │     └─ lw_update_leds                               │
   └─────────────────────────────────────────────────────┘
                             │
                             │ osMutex: snapshot field_values.values
                             ▼
   ┌─────────────────────────────────────────────────────┐
   │  task_sd  (osPriorityBelowNormal)                   │
   │                                                     │
   │  Loop (osDelayUntil, periodic log_interval_ms):     │
   │     ├─ mutex { memcpy snapshot ← field_values }     │
   │     └─ lw_write_snapshot → io_buf → f_write/f_sync  │
   │        (blocks in SD_write on a GC stall —          │
   │         only this task waits, CAN drain keeps on)   │
   └─────────────────────────────────────────────────────┘
```

## Config file format (config.ini)

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

### `[logger]` section

| Key | Required | Default | Description |
|-----|----------|---------|-------------|
| `interval_ms` | yes | — | Data-block write interval (ms) |
| `can_bitrate` | no | 500000 | CAN bus speed (125000/250000/500000/1000000) |

### `[field]` sections

| Key | Required | Default | Description |
|-----|----------|---------|-------------|
| `can_id` | yes* | — | CAN ID (hex `0x...` or decimal). *Not needed if `demo_func` is set |
| `is_extended` | no | 0 | 1 = 29-bit extended ID (AEMnet, J1939), 0 = 11-bit standard |
| `name` | yes | — | Parameter name (up to 33 chars) |
| `units` | yes | — | Units of measurement (up to 9 chars) |
| `start_byte` | yes | — | Starting byte in CAN data (0–7) |
| `start_bit` | no | 0 | Starting bit within the byte (0–7); only for `bit_length < 8` |
| `bit_length` | yes | — | Data length in bits: 1–7 (sub-byte), 8, 16, 32, 64 |
| `type` | yes | — | Type: U08, S08, U16, S16, U32, S32, S64, F32 |
| `scale` | yes | — | Display multiplier |
| `offset` | yes | — | Display offset |
| `is_big_endian` | no | 0 | Byte order in the CAN frame (0=LE, 1=BE). Ignored for sub-byte |
| `digits` | no | 0 | Decimal places |
| `display_style` | no | 0 | Display style (0=Float) |
| `category` | no | "" | Category used to group fields in MLV |
| `lut` | no | — | Lookup table for non-linear conversion (see below) |
| `valid_min` | no | — | Lower bound of the plausibility filter (display units; applied after scale/offset/LUT) |
| `valid_max` | no | — | Upper bound of the plausibility filter (display units) |
| `invalid_strategy` | no | `last_good` | Reaction to outliers: `last_good` (repeat the previous value), `clamp` (saturate at the bound), `skip` (do not update the shadow) |
| `preset` | no | `none` | Sensor-specific fault preset that complements `valid_min/max`. `aem_uego` rejects raw `0xFFFF` on the 16-bit field (AEM warm-up / free-air cal / sensor fault). Applied before `valid_min/max` |
| `demo_func` | no | — | Demo data generator function: sine/ramp/square/noise/const |
| `demo_min` | no | 0 | Minimum value (display units) |
| `demo_max` | no | 0 | Maximum value (display units) |
| `demo_period_ms` | no | 5000 | Period for sine/ramp/square (ms) |
| `demo_smoothing` | no | 0.95 | Smoothing for noise (0.0–1.0, higher = smoother) |

- The order of `[field]` sections defines the field order in MLG
- Several fields may reference the same `can_id`, but `is_extended` must match — otherwise `CFG_ERR_VALUE`
- Unique CAN IDs are collected automatically for hardware-filter setup; the filter picks std/ext based on `is_extended`
- Byte-aligned validation: `bit_length` is a multiple of 8, `start_byte + bit_length/8 <= 8`, `start_bit == 0`
- Sub-byte validation: `bit_length` 1–7, `start_bit + bit_length <= 8`, type = `U08` or `S08`
- Demo fields (with `demo_func`) do not require `can_id`/`start_byte`/`bit_length` — type and size come from `type`
- Demo mode is enabled automatically when at least one field has `demo_func`
- CAN and demo fields can coexist in a single config (hybrid mode)
- Maximum config.ini size — 8 KB

### Lookup Table (LUT)

For non-linear sensors (NTC thermistors, non-linear pressure sensors), a piecewise-linear conversion table can be defined:

```ini
lut = input1:output1, input2:output2, ...
```

- `input` — raw value from CAN (uint16, e.g. mV)
- `output` — value in display units (int16, e.g. °C or kPa)
- Minimum 2 points, maximum 16
- Points must be sorted by ascending input
- Linear interpolation between points, clamp outside the ends
- When a LUT is present, `scale`/`offset` are used for MLG storage (stored = display / scale - offset)

Example — NTC thermistor (Bosch 0261230042, pull-up 2.2 k):

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

Example — linear MAP (2 points from the datasheet):

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

## Lifecycle (FreeRTOS)

### Initialisation (main.c → StartDefaultTask)

```
main():
  1. HAL_Init(), SystemClock_Config()
  2. GPIO, DMA, CAN1, SDIO, FATFS, RTC init (CubeMX)
  3. ring_buf_init()
  4. osKernelInitialize() → create shadow_mutex, defaultTask, sdTask
  5. osKernelStart() — control passes to the scheduler

StartDefaultTask (task_producer):
  6. MX_USB_DEVICE_Init() — USB CDC
  7. lw_init() — SD mount, config parse, MLG header+fields write
  8. can_drv_init/start — CAN RX (or sniffer mode if there is no config)
  9. → main task_producer loop
```

### Tasks (runtime)

**task_producer** (osPriorityNormal) — CAN drain + GPS drain + debug CLI:
```
for (;;) {
    demo_pack_can_frames()              // if demo mode
    gps_drv_poll()                      // drain DMA, parse NMEA, raw-tap putchar
    mutex {
        can_map_update_gps()            // if [gps] enable = 1
        while (ring_buf_pop(&frame))
            can_map_process(→ field_values shadow)
        demo_generate()                 // legacy direct path
    }
    // one-shot RTC sync on the first fix with date+time (has_fix && has_date && has_time)
    debug_out_tick / debug_cmd_poll     // USB CDC CLI
    lw_update_leds()
    osDelay(1)
}
```

**task_sd** (osPriorityBelowNormal) — SD writer:
```
for (;;) {
    osDelayUntil(next_wake)         // periodic, drift-free
    mutex { memcpy snapshot ← field_values }
    lw_write_snapshot(snapshot)     // io_buf → f_write → f_sync → rotate
}
```

### Shutdown (K1 button → EXTI ISR → lw_shutdown = 1)

```
task_producer: can_drv_stop(), osDelay(50), osThreadExit()
task_sd:       lw_stop() (flush + sync + truncate + close), osThreadExit()
```

## Error handling

| Situation | Behaviour |
|-----------|-----------|
| No SD card | `sd_init()` fails → LED_ERROR, do not start |
| No config.ini | `sd_read_file()` fails → LED_NO_CONFIG, do not start |
| Invalid config | `cfg_parse()`/`cfg_validate()` fails → LED_NO_CONFIG, do not start |
| SD write error | Retry up to N times, then LED_ERROR, stop writing |
| Ring buffer overflow | `ring_buf_push()` returns an error, the frame is dropped (loss counter) |
| File hits 4 MB | Close the current one, open a new one (rotation) |

## Test strategy

### Unit tests (Lib/, each module in isolation)

| Module | What we test |
|--------|--------------|
| config | Valid INI parsing, syntax errors, edge values, invalid types |
| can_map | Extracting U08/U16/U32/F32 from CAN data, big/little endian, scale/offset, multiple IDs |
| mlvlg | Header/field/data block/marker format, endianness, CRC, buffer overflow |
| ring_buf | Push/pop, empty/full, wraparound, FIFO order |

### Snapshot tests (intermediate structures)

Reference binary buffers for:
- Serialised MLG header (24 bytes) for a given set of fields
- Serialised field descriptor (89 bytes) for a specific field
- Serialised data block with known input values

The test compares the output byte-for-byte against a recorded reference. When the format changes, the reference is updated explicitly.

### Snapshot tests (full file)

- Build a complete `.mlg` file from a config + a set of CAN frames
- Byte-compare against a reference `.mlg`
- Additionally: run it through `mlg-converter` (Node.js) and check that parsing succeeds

### Host-side integration tests

Full pipeline without HAL:
1. `cfg_parse(ini_text)` → `cfg_Config`
2. `can_map_init(cfg)` → `can_FieldValues`
3. A series of `can_map_process(frame)` calls with test frames
4. `mlg_write_header()` + `mlg_write_field()` + `mlg_write_data_block(shadow_values)`
5. Write the result to a file
6. Validate: `node mlg-test/validate.js output.mlg` → parse + check values

### E2E test (on hardware)

cansult (CAN source) → canlogger → SD → PC → MegaLogViewer

## File layout (target)

```
firmware/
├── Lib/                    # Pure logic (host-testable)
│   ├── mlvlg.{h,c}        # MLVLG v2 encoder
│   ├── config.{h,c}       # INI config parser
│   ├── can_map.{h,c}      # CAN → field-values mapping
│   ├── demo_gen.{h,c}     # Demo data generator (sine/ramp/square/noise/const)
│   ├── cfg_limits.h        # Shared limits (CFG_MAX_FIELDS, CFG_MAX_CAN_IDS)
│   └── ring_buf.{h,c}     # Lock-free SPSC ring buffer
├── Src/                    # HAL-dependent wrappers
│   ├── main.c              # Init, FreeRTOS tasks (task_producer, task_sd), glue
│   ├── can_drv.c           # CAN HAL → ring_buf
│   ├── log_writer.c        # SD: config read, MLG write, file rotation, error recovery, FAULT file
│   ├── sd_write_dma.c      # BSP override: DMA write fix, SDIO error counters
│   ├── sd_diskio.c         # CubeMX-generated + instrumented SD_status (retry loop) and SD_write wrappers
│   ├── debug_out.c         # USB CDC CLI (help/status/stream/config/ls/get/put)
│   └── [CubeMX files]
├── Inc/                    # Headers for Src/
│   ├── can_drv.h
│   ├── log_writer.h
│   ├── sd_write_dma.h
│   ├── sd_diskio_counters.h # sd_sdio_Counters (per-failure-point diagnostics)
│   └── [CubeMX headers]
├── test/                   # Host tests
│   ├── unity/
│   ├── test_mlvlg.c
│   ├── test_config.c
│   ├── test_can_map.c
│   ├── test_ring_buf.c
│   ├── test_integration.c
│   ├── snapshots/          # Reference binary files
│   └── Makefile
└── Makefile                # build, test, flash, debug
```
