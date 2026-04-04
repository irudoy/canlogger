# Reference Documentation

## MLVLG Format Specification (official)

- [MLG_Binary_LogFormat_1.0.pdf](MLG_Binary_LogFormat_1.0.pdf) — MLVLG v1 spec (Logger Field = 55 bytes, Info Data Start = 2 bytes)
- [MLG_Binary_LogFormat_2.0.pdf](MLG_Binary_LogFormat_2.0.pdf) — MLVLG v2 spec (Logger Field = 89 bytes with Category, Info Data Start = 4 bytes)

Author: Philip Tobin (EFI Analytics / TunerStudio / MegaLogViewer)

### Key differences v1 vs v2

| Field            | v1           | v2           |
|------------------|--------------|--------------|
| Format Version   | 0x0001       | 0x0002       |
| Info Data Start  | 2 bytes (offset 12) | 4 bytes (offset 12) |
| Header size      | 22 bytes     | 24 bytes     |
| Logger Field     | 55 bytes (no Category) | 89 bytes (+ 34-byte Category) |
| Logger Field[]   | starts at offset 22 | starts at offset 24 |

### Display value formula

```
DisplayValue = (rawValue + transform) * scale
rawValue = DisplayValue / scale - transform
```

### CRC

1-byte overflow sum of all data bytes in Logger Field Record (not including block type, counter, timestamp header).

### Timestamp in data blocks

2-byte timestamp at 10 us/bit resolution.

## rusefi Reference Implementation

Source files from [rusefi/rusefi](https://github.com/rusefi/rusefi/tree/master/firmware/console/binary_mlg_log):

- [rusefi_mlg_types.h](rusefi_mlg_types.h) — Header/field constants, type enums, size helpers
- [rusefi_mlg_field.h](rusefi_mlg_field.h) — `Field` class: writeHeader() and writeData() with endian swap
- [rusefi_binary_mlg_logging.cpp](rusefi_binary_mlg_logging.cpp) — writeFileHeader() and writeSdBlock() full implementation
- [rusefi_binary_mlg_logging.h](rusefi_binary_mlg_logging.h) — Public API

## Hardware

- [STM32F407VET6-STM32_F4VE_V2.0_schematic.pdf](STM32F407VET6-STM32_F4VE_V2.0_schematic.pdf) — Board schematic

### Board features (from schematic)

- MCU: STM32F407VET6 (196K RAM, 512K Flash, 168MHz)
- Power: AMS1117-3.3V regulator, 5V input via header
- Crystals: 8MHz HSE (Y2), 32.768KHz LSE (Y1) for RTC
- SD card: MiniSD slot (U5) via SDIO
- USB: Mini USB (J4) as slave
- JTAG/SWD: 20-pin JTAG header (P1)
- ISP: UART1 (PA9 TX, PA10 RX) via 4-pin header (J6)
- NRF2401: 4-pin dual-row header (JP2)
- External Flash: W25Q16 (U3) SPI
- LEDs: D1, D2, D3 (active low)
- Buttons: K0, K1 (active low with pull-up), K_UP (active high), RST
- TFT LCD: 24x2 dual-row headers (J2, J3) + 16x2 header (J1)
- Boot mode: configurable via BOOT0/BOOT1 resistors
