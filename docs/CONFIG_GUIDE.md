# CAN Logger: Configuration & MLG Format Guide

## Quick Start

1. Create `config.ini` on FAT32-formatted SD card (root directory)
2. Insert SD card into the logger
3. Power on — LED D2 blinks = logging active
4. Power off, eject SD — open `.mlg` files in [MegaLogViewer](https://www.efianalytics.com/MegaLogViewer/)

If there is no `config.ini` or it has errors, LEDs will indicate an error (see [HARDWARE.md](HARDWARE.md)).

## Config File Format

The config file uses INI-like syntax. It has one `[logger]` section and one or more `[field]` sections.

```ini
[logger]
interval_ms = 50
can_bitrate = 500000

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
```

Comments start with `#` and are ignored.

### [logger] Section

| Key | Required | Default | Description |
|-----|----------|---------|-------------|
| `interval_ms` | yes | -- | How often to write a data record (ms). 50 = 20 Hz, 10 = 100 Hz |
| `can_bitrate` | no | 500000 | CAN bus speed. Allowed: 125000, 250000, 500000, 1000000 |

### [field] Sections

Each `[field]` section defines one logged parameter. The order of sections determines the order of fields in the MLG file and in MegaLogViewer.

| Key | Required | Default | Description |
|-----|----------|---------|-------------|
| `can_id` | yes | -- | CAN ID to listen for (hex `0x...` or decimal) |
| `name` | yes | -- | Parameter name shown in MegaLogViewer (max 33 chars) |
| `units` | yes | -- | Units label (max 9 chars) |
| `start_byte` | yes | -- | Byte position in CAN data payload (0--7) |
| `bit_length` | yes | -- | Data width in bits: 8, 16, 32, or 64 |
| `type` | yes | -- | Data type: `U08`, `S08`, `U16`, `S16`, `U32`, `S32`, `S64`, `F32` |
| `scale` | yes | -- | Display multiplier: `display = raw * scale + offset` |
| `offset` | yes | -- | Display offset |
| `is_big_endian` | no | 0 | Byte order in CAN frame: 0 = little-endian, 1 = big-endian |
| `digits` | no | 0 | Decimal places in MegaLogViewer |
| `display_style` | no | 0 | 0=Float, 1=Hex, 2=Bits, 4=On/Off, 5=Yes/No, 6=High/Low |
| `category` | no | *(empty)* | Group name in MegaLogViewer (max 33 chars) |
| `lut` | no | -- | Lookup table for non-linear conversion (see below) |

### Constraints

- Maximum 32 fields total
- `start_byte + bit_length/8` must not exceed 8 (CAN frame is 8 bytes)
- Multiple fields can reference the same `can_id`
- Field names must be unique

## Data Conversion: scale & offset

The logger stores raw integer values extracted from CAN data. MegaLogViewer displays them using:

```
DisplayValue = raw * scale + offset
```

**Examples:**

| Sensor | Raw range | Scale | Offset | Display range |
|--------|-----------|-------|--------|---------------|
| Battery voltage | 0--255 | 0.08 | 0 | 0--20.4 V |
| Coolant temp (Nissan) | 0--255 | 1.0 | -50.0 | -50--205 C |
| RPM (16-bit) | 0--65535 | 12.5 | 0 | 0--819187 rpm |
| Ignition timing | 0--255 | -1.0 | -110.0 | -110--145 deg |

## Lookup Tables (LUT)

For non-linear sensors (NTC thermistors, non-linear pressure sensors), use a lookup table instead of simple scale/offset.

```ini
lut = input1:output1, input2:output2, ...
```

- `input` -- raw value from CAN (unsigned 16-bit, e.g. millivolts from ADC)
- `output` -- display value (signed 16-bit, e.g. degrees C)
- 2 to 16 points, sorted by ascending input
- Linear interpolation between points, clamped outside range
- When LUT is present, `scale`/`offset` are still required -- they define how the display value is stored in MLG format (`stored = display / scale - offset`)

### Example: NTC thermistor (Bosch 0261230042, 2.2k pull-up)

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
category = Sensors
lut = 192:120, 325:100, 428:90, 570:80, 760:70, 1014:60, 1346:50, 1765:40, 2259:30, 2807:20, 3353:10, 3848:0, 4543:-20, 4860:-40
```

Raw 2259 mV -> 30 C, raw 3000 mV -> interpolated ~16 C, raw 100 mV -> clamped 120 C.

### Example: linear MAP sensor (2-point LUT from datasheet)

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
category = Sensors
lut = 400:20, 4650:250
```

## Byte Order (Endianness)

CAN data bytes can be in little-endian (LSB first) or big-endian (MSB first) order depending on the source ECU.

- `is_big_endian = 0` (default): bytes `[low, high]` -- common in many aftermarket ECUs
- `is_big_endian = 1`: bytes `[high, low]` -- common in OEM ECUs, Nissan Consult

For 8-bit fields (`bit_length = 8`), endianness does not matter.

## Choosing the Right Data Type

| CAN data | Type | Bit length | Notes |
|----------|------|------------|-------|
| Unsigned byte (0--255) | `U08` | 8 | Most common for single-byte values |
| Signed byte (-128--127) | `S08` | 8 | Temperature with negative range |
| Unsigned 16-bit (0--65535) | `U16` | 16 | RPM, injection time, ADC values |
| Signed 16-bit (-32768--32767) | `S16` | 16 | Temperature from LUT |
| Unsigned 32-bit | `U32` | 32 | Odometer, counters |
| Signed 32-bit | `S32` | 32 | |
| Signed 64-bit | `S64` | 64 | Uses full CAN frame |
| 32-bit float | `F32` | 32 | IEEE 754 floating point |

## MLG File Format (for users)

The logger writes `.mlg` files in **MLVLG v2** format, compatible with [MegaLogViewer](https://www.efianalytics.com/MegaLogViewer/) (free and HD versions).

- Files are named sequentially: `LOG_0001.MLG`, `LOG_0002.MLG`, ...
- File rotation at 4 MB -- new file is started automatically
- All data is big-endian
- Each data record contains a snapshot of all field values at the time of writing

### Opening in MegaLogViewer

1. File -> Open Log File -> select `.mlg` file
2. All configured fields appear in the field list with their names, units, and categories
3. Drag fields to the graph area to plot
4. Use categories to group related fields

### Display formula

MegaLogViewer applies the formula from the MLG header:

```
DisplayValue = (storedValue + transform) * scale
```

This is set up automatically by the logger from your config -- you don't need to configure anything in MegaLogViewer.

## Complete Example: Nissan Consult via cansult

This config logs 11 parameters from a Nissan ECU using the [cansult](../cansult/) bridge (Consult protocol -> CAN bus).

```ini
[logger]
interval_ms = 50
can_bitrate = 500000

# Connection state
[field]
can_id = 0x665
name = State
units = raw
start_byte = 0
bit_length = 8
type = U08
scale = 1.0
offset = 0.0
digits = 0
category = Cansult

# Battery voltage: raw 0-255, display 0-20.4V
[field]
can_id = 0x666
name = Battery
units = V
start_byte = 0
bit_length = 8
type = U08
scale = 0.08
offset = 0.0
digits = 1
category = Engine

# Coolant temperature: raw 0-255, display -50 to 205 C
[field]
can_id = 0x666
name = Coolant
units = C
start_byte = 1
bit_length = 8
type = U08
scale = 1.0
offset = -50.0
digits = 0
category = Engine

# RPM: 16-bit big-endian, raw * 12.5 = RPM
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

# Vehicle speed: raw * 2 = km/h
[field]
can_id = 0x667
name = Speed
units = km/h
start_byte = 0
bit_length = 8
type = U08
scale = 2.0
offset = 0.0
digits = 0
category = Vehicle
```

## Troubleshooting

| Symptom | Cause | Fix |
|---------|-------|-----|
| LED error on startup | No `config.ini` on SD root | Create config file, check FAT32 format |
| LED error on startup | Syntax error in config | Check field names, missing values, typos |
| All values zero | Wrong `can_id` | Verify CAN IDs from your source device |
| Values look wrong | Wrong `is_big_endian` | Try toggling between 0 and 1 |
| Values look wrong | Wrong `scale`/`offset` | Check ECU documentation for conversion formula |
| Values look wrong | Wrong `start_byte` | Verify byte position in CAN data payload |
| No log files | SD card issue | Use FAT32-formatted card, check contacts |
| Short log files | Power loss | Use stable power supply, graceful shutdown with K1 button |
