# Debug: USB CDC

## Подключение

Micro-USB кабель от платы к ПК. На macOS появляется как `/dev/cu.usbmodemXXXX`.

```bash
# Найти порт
ls /dev/cu.usbmodem*

# Подключиться
screen /dev/cu.usbmodemXXXX 115200
```

## Вывод

Каждую секунду:

```
[42] frames=5000 fields=13 init=1
  0x640: 08 B9 07 54 00 00 00 00  A1=2233mV A2=1876mV
```

| Поле | Описание |
|------|----------|
| `[42]` | Секунды с момента старта |
| `frames` | Количество обработанных CAN-фреймов |
| `fields` | Количество полей в конфиге |
| `init` | 1 = конфиг загружен, логирование идёт; 0 = ошибка |
| `0x640:` | Сырые байты последнего фрейма с CAN ID 0x640 (если есть) |
| `A1/A2` | Аналоговые входы switchboard в мВ (big-endian U16) |

## USB параметры

- USB OTG FS, Device Only, Full Speed 12 Mbit/s
- VID/PID: 0x0483/0x5740 (STMicroelectronics CDC)
- Product: "CANLogger Debug Port"
- VBUS sensing: отключен
- IRQ приоритет: 7 (ниже CAN = 5)
- Буфер: 256 байт, flush по `\n` или при заполнении
- При BUSY (хост не подключён) — данные молча отбрасываются

## OpenOCD / GDB отладка

Все команды из `firmware/`.

### `make ocd-server`

Запускает OpenOCD GDB-сервер на порту :3333. Нужен для всех `ocd-*` команд.

```bash
make ocd-server  # в отдельном терминале
```

### `make ocd-debug`

Собирает, подключается через GDB, загружает прошивку. Интерактивная GDB сессия.

### `make ocd-status`

Одноразовый снимок состояния устройства (3 секунды работы → halt → dump):

```
=== State ===
init_ok=1 error=0 shutdown=0
file=06003800.MLG blocks=59
=== Config ===
interval=50 bitrate=500000 fields=13 can_ids=4
=== CAN ===
rb head=30 tail=30
ESR=0x00000000 RF0R=0x0000001B BTR=0x004E0003 IER=0x00000002
=== Field Values ===
updated=1 values: 0 147 130 55 0 255 30
```

### `make ocd-dump`

Расширенный дамп (5 секунд работы, скрипт `scripts/dump.gdb`).

### `make gdb-server` / `make debug`

Альтернатива через ST-LINK GDB Server (требует свежую прошивку ST-Link).

### Важно

- OpenOCD и ST-LINK Programmer не могут работать одновременно (оба захватывают SWD)
- Перед `make flash` убейте OpenOCD: `pkill -f openocd`

## Архитектура

Модуль `Src/debug_out.c`:
- `__io_putchar()` → буферизованный `CDC_Transmit_FS()`
- `debug_out_tick()` — вызывается из main loop, печатает статус раз в секунду
- `debug_out_set_can640()` — захват сырого CAN фрейма для отображения

Main.c добавляет минимум:
- `#include "debug_out.h"`
- `debug_out_tick(...)` в main loop
- `debug_out_set_can640(...)` при получении фрейма 0x640
