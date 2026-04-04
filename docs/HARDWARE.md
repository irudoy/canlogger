# Hardware: CAN Logger

## Базовая плата

**STM32F407VET6 — STM32_F4VE V2.0**

- MCU: STM32F407VET6, 168 MHz, 192KB RAM, 512KB Flash
- Документация: https://stm32-base.org/boards/STM32F407VET6-STM32-F4VE-V2.0.html
- Схема: `reference/STM32F407VET6-STM32_F4VE_V2.0_schematic.pdf`

### Используемые периферии

| Периферия | Пины | Назначение | Статус |
|-----------|------|------------|--------|
| SDIO | PC8, PC9, PC10, PC11, PC12, PD2 | SD-карта (1-bit mode) | Настроено |
| RTC | — | Часы реального времени (LSI) | Настроено |
| LED1 | PA6 | Индикация (active-low) | Настроено |
| LED2 | PA7 | Индикация (active-low) | Настроено |
| K1 | PE3 | Кнопка shutdown (EXTI rising) | Настроено |
| CAN1 | PB8 (RX), PB9 (TX) | CAN-шина 500 kbit/s | Планируется |
| SWD | PA13 (SWDIO), PA14 (SWCLK) | Отладка | Настроено |

### LED индикация

| LED1 | LED2 | Состояние |
|------|------|-----------|
| Горит | Мигает (500ms) | Нормальная запись |
| Мигает (100ms) | Мигает (100ms) | Ошибка |
| Выключен | Горит | Остановлен |

### Кристаллы

- HSE: 8 MHz (Y2) → PLL → 168 MHz SYSCLK
- LSE: 32.768 kHz (Y1) — для RTC (пока используется LSI)

### JTAG/SWD разъём (P1, 20-pin ARM standard)

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

### Подключение ST-Link V2 (10-pin dongle → 20-pin P1)

| ST-Link dongle | Сигнал | → Плата P1 |
|---|---|---|
| Pin 8 | SWCLK | Pin 9 |
| Pin 9 | SWDIO | Pin 7 |
| Pin 10 | GND | Pin 4 (или любой чётный) |
| Pin 16 | RST | Pin 15 (NRST) |

**Важно:** pin номера на донгле и плате не совпадают! SWDIO на донгле = pin 9, на плате = pin 7.

### Кнопки

| Кнопка | Пин | Тип | Назначение |
|--------|-----|-----|------------|
| K0 | PE4 | Active-low, pull-up | Не используется |
| K1 | PE3 | Active-low, pull-up | Shutdown (EXTI) |
| K_UP | PA0 | Active-high, pull-down | Не используется |
| RST | NRST | — | Аппаратный сброс MCU |

## Hat (расширительная плата)

Планируется кастомная плата поверх отладочной:

- **CAN-трансивер** (MCP2551 / TJA1050) → PB8 (CAN1_RX), PB9 (CAN1_TX)
- **Automotive DC-DC конвертер** — 12V → 5V/3.3V
- **Graceful shutdown** — суперконденсатор + обнаружение падения напряжения

### Схема подключения hat

```
  Питание                    CAN                    Shutdown
  ────────                   ───                    ────────

  12V (авто) ──► DC-DC ──► 5V ──► 3.3V LDO        12V ──► делитель ──► ADC pin
                            │                               (VIN_SENSE)
                            ├──► Supercap
                            │
                            ▼
                 ┌──────────────────┐
                 │  CAN Transceiver │
                 │  MCP2551/TJA1050 │
                 │                  │
    PB9 (TX) ──► │ TXD          CANH│ ──►─┐
    PB8 (RX) ◄── │ RXD          CANL│ ──►─┤ CAN Bus
         5V  ──► │ VCC              │     │
        GND  ──► │ GND              │  [120Ω]
                 └──────────────────┘     │
                                          ▼
                                     К автомобилю

  ════════════ pin headers ════════════
  │  PB8  │  PB9  │  5V  │  GND  │ ADC │
  ═════════════════════════════════════
  ┌───────────────────────────────────┐
  │       STM32_F4VE V2.0 Board       │
  │              (снизу)              │
  └───────────────────────────────────┘
```

### Подключение (текстовое описание)

- STM32 **PB9** (CAN1_TX) → CAN Transceiver **TXD**
- STM32 **PB8** (CAN1_RX) → CAN Transceiver **RXD**
- CAN Transceiver **VCC** → 5V от DC-DC
- CAN Transceiver **GND** → общий GND
- CAN Transceiver **CANH** → CAN Bus High
- CAN Transceiver **CANL** → CAN Bus Low
- **120 Ом** терминирующий резистор между CANH и CANL на hat
- **12V IN** → DC-DC → 5V → 3.3V LDO → питание платы
- **VIN_SENSE** → ADC пин STM32 для детекции потери питания (graceful shutdown)
- **Суперконденсатор** на 5V для поддержания питания при shutdown

CAN bus: 500 kbit/s, терминация 120 Ом на каждом конце шины.

## E2E тестовый стенд

```
[cansult] ──CANH/CANL──┬── [canlogger hat]
                        │
                   120Ω termination (on each device)
```

Источник данных: проект `../cansult` (Nissan Consult → CAN adapter)
- MCU: STM32F103TBU6
- CAN-трансивер: MCP2562
- 3 сообщения @ 20Hz, 500 kbit/s
- CAN IDs: 0x666, 0x667, 0x668
