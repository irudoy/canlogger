# Hardware: CAN Logger

## Base board

**STM32F407VET6 — STM32_F4VE V2.0**

- MCU: STM32F407VET6, 168 MHz, 192 KB RAM, 512 KB Flash
- Documentation: https://stm32-base.org/boards/STM32F407VET6-STM32-F4VE-V2.0.html
- Schematic: `reference/STM32F407VET6-STM32_F4VE_V2.0_schematic.pdf`

### Peripherals in use

| Peripheral | Pins | Purpose | Status |
|------------|------|---------|--------|
| SDIO | PC8, PC9, PC10, PC11, PC12, PD2 | SD card (4-bit mode) | Configured |
| RTC | — | Real-time clock (LSI) | Configured |
| LED1 (D2) | PA6 | Indicator (active-low) | Configured |
| LED2 (D3) | PA7 | Indicator (active-low) | Configured |
| K1 | PE3 | Shutdown button (EXTI rising) | Configured |
| CAN1 | PB8 (RX), PB9 (TX) | CAN bus 500 kbit/s | Configured |
| USB_OTG_FS | PA11 (DM), PA12 (DP) | USB CDC debug output | Configured |
| SWD | PA13 (SWDIO), PA14 (SWCLK) | Debug | Configured |

### LED indication

| LED1 | LED2 | State |
|------|------|-------|
| On | Blinking (500 ms) | Normal recording |
| Blinking (100 ms) | Blinking (100 ms) | Error |
| Off | On | Stopped |

### Crystals

- HSE: 8 MHz (Y2) → PLL → 168 MHz SYSCLK
- LSE: 32.768 kHz (Y1) — for RTC (currently uses LSI)

### JTAG/SWD header (P1, 20-pin ARM standard)

```
Pin 1  = VCC        Pin 2  = VCC
Pin 3  = TRST       Pin 4  = GND
Pin 5  = TDI        Pin 6  = GND
Pin 7  = TMS/SWDIO  Pin 8  = GND
Pin 9  = TCK/SWCLK  Pin 10 = GND
Pin 11 = RTCK       Pin 12 = GND
Pin 13 = TDO/SWO    Pin 14 = GND
Pin 15 = NRST       Pin 16 = GND
Pin 17 = NC         Pin 18 = GND
Pin 19 = NC         Pin 20 = GND
```

### ST-Link V2 wiring (10-pin dongle → 20-pin P1)

| ST-Link dongle | Signal | → Board P1 |
|---|---|---|
| Pin 8 | SWCLK | Pin 9 |
| Pin 9 | SWDIO | Pin 7 |
| Pin 10 | GND | Pin 4 (or any even pin) |
| Pin 16 | RST | Pin 15 (NRST) |

**Note:** pin numbers on the dongle and the board do not match. SWDIO is pin 9 on the dongle, pin 7 on the board.

### Buttons

| Button | Pin | Type | Purpose |
|--------|-----|------|---------|
| K0 | PE4 | Active-low, pull-up | Log marker (EXTI falling, 300 ms any-edge debounce, D2 blinks for ~75 ms) |
| K1 | PE3 | Active-low, pull-up | Shutdown (EXTI) |
| K_UP | PA0 | Active-high, pull-down | Unused |
| RST | NRST | — | MCU hardware reset |

## Current CAN input prototype

Modified MCP2515 board (the MCP2515 controller itself is unused — only the TJA1050 transceiver is kept):

```
  STM32 F4VE                   Modified MCP2515 board
  ──────────                   ──────────────────────
  PB9 (CAN1_TX) ──────────► SO (was SPI MISO, now TJA TXD)
  PB8 (CAN1_RX) ◄────────── SI (was SPI MOSI, now TJA RXD)
  GND ──────────────────────── GND
  5V ───────────────────────── VCC
```

The CAN TX/RX signals go to the SO/SI pins of the MCP2515 board, which are routed straight to the TJA1050 transceiver (the MCP2515 controller is bypassed).

## Hat (expansion board, plan)

Full spec — [HAT_PCB.md](HAT_PCB.md). Summary below.

Stacking hat that seats on J2 (24×2) and J3 (24×2) of the F4VE board. It contains:

- **12 V input protection** — fuse (PPTC) + TVS SMCJ28CA + reverse-polarity P-MOSFET DMG2305UX
- **DC-DC 12 V → 5 V** — LM2596S-5.0, ~500 mA
- **Graceful shutdown** — 1N5819 + supercap KAMCAP 2.5 F 5.5 V
- **VIN_SENSE divider** — 10 k/10 k on the DC-DC output → PA0 ADC
- **CAN transceiver** MCP2562FD + ESDCAN24-2BLY + switchable 120 Ω + ferrite → PB8/PB9
- **On-hat K0/K1 buttons** with RC debounce (100 nF + 1 kΩ) → PE4/PE3
- **Active buzzer** 5 V through N-FET AO3400 → PE5
- **GPS header** (NEO-6M / M8N): UART PB10/PB11 + 1PPS on PA1

Pin allocation:

| Function | STM32 | F4VE header |
|---|---|---|
| CAN_TX | PB9 | J3.13 |
| CAN_RX | PB8 | J3.14 |
| VIN_SENSE | PA0 | J2.23 |
| K0 marker | PE4 | J2.13 |
| K1 shutdown | PE3 | J2.12 |
| Buzzer | PE5 | J2.14 |
| GPS UART TX | PB10 | J2.44 |
| GPS UART RX | PB11 | J2.45 |
| GPS 1PPS | PA1 | J2.24 |

CAN bus: 500 kbit/s, 120 Ω termination (switchable jumper) on each end of the bus. Schematic, BOM, and layout details in [HAT_PCB.md](HAT_PCB.md).

## USB CDC (debug output)

The on-board micro-USB connector exposes a USB CDC Virtual COM Port. On macOS it shows up as `/dev/cu.usbmodemXXXX`.

- USB_OTG_FS: Device Only, Full Speed 12 Mbit/s
- VBUS sensing: disabled (device-only, no ID pin)
- IRQ priority: 7 (below CAN = 5)
- Product string: "CANLogger Debug Port"
- Buffered printf via `__io_putchar` → `CDC_Transmit_FS`

## E2E test bench

```
[cansult] ──CANH/CANL──┬── [canlogger hat]
                        │
                   120Ω termination (on each device)
```

Data source: the `../cansult` project (Nissan Consult → CAN adapter)
- MCU: STM32F103TBU6
- CAN transceiver: MCP2562
- 3 messages @ 20 Hz, 500 kbit/s
- CAN IDs: 0x666, 0x667, 0x668
