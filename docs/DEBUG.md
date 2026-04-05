# Debug: USB CDC

## Подключение

Micro-USB кабель от платы к ПК. На macOS появляется как `/dev/cu.usbmodemXXXX`.

```bash
# Найти порт
ls /dev/cu.usbmodem*

# Подключиться (рекомендуется picocom)
picocom /dev/cu.usbmodemXXXX -b 115200

# Выход из picocom: Ctrl-A Ctrl-X
```

Установка: `brew install picocom`

## CLI-команды

При подключении устройство молчит (stream выключен). Набирайте команды, Enter для отправки. Ввод отображается (echo).

| Команда | Описание |
|---------|----------|
| `help` | Список команд |
| `status` | Одноразовый снимок состояния |
| `stream` | Включить периодический вывод (1 раз/сек). Любой ввод выключает stream |
| `config` | Показать загруженный конфиг |
| `ls` | Список файлов на SD с размерами |
| `get <f>` | Скачать файл с SD (используйте `usb_get.py`) |

### `help`

```
Commands:
  help    - this message
  status  - system status
  stream  - toggle periodic output
  config  - show loaded config
  ls      - list MLG files on SD
  get <f> - download file (use usb_get.py)
```

### `status`

```
uptime=10s frames=221 fields=13 init=1 stream=0
file=06041100.MLG size=5942 files=1 blocks=207 err=0/0
rb: count=0 head=29 tail=29
sd: 15708480KB free / 15712248KB total
can: 2 ids
  0x640[8] 11ms ago: 08 AF 07 4F 00 00 00 00
  0x665[8] 70ms ago: 02 FF FF FF FF FF FF FF
```

| Строка | Описание |
|--------|----------|
| `uptime` | Секунды с момента старта |
| `frames` | Количество обработанных CAN-фреймов |
| `fields` | Количество полей в конфиге |
| `init` | 1 = конфиг загружен, логирование идёт; 0 = ошибка |
| `stream` | 1 = периодический вывод включён |
| `file` | Текущий MLG файл, размер в байтах |
| `files` | Сколько файлов создано с момента старта |
| `blocks` | Количество data blocks в текущем файле |
| `err=N/S` | N = счётчик ошибок SD, S = error state (1 = фатальная ошибка) |
| `rb` | Ring buffer: count = фреймов в очереди, head/tail = позиции |
| `sd` | Свободное / общее место на SD карте |
| `can` | Количество уникальных CAN ID на шине |
| `0x640[8]` | CAN ID, DLC, сырые байты, время с последнего обновления |

### `stream`

Включает периодический вывод (как `status`, каждую секунду). Любое нажатие клавиши автоматически выключает stream — можно сразу вводить следующую команду.

```
[42] frames=5000 fields=13 init=1 ids=4
  0x666[8]: 26 02 37 00 FF 1E 00 00
  0x667[8]: 00 00 00 01 09 00 0E FF
```

### `config`

```
bitrate=500000 interval=50ms fields=13 can_ids=4
  [0] 0x665 b0:8 State (raw) s=1.000 o=0.00
  [1] 0x666 b0:8 Battery (V) s=0.080 o=0.00
  [2] 0x666 b1:8 Coolant (C) s=1.000 o=-50.00
  ...
  [12] 0x640 b2:16 MAP (kPa) s=1.000 o=0.00 lut=2
```

Формат: `[индекс] CAN_ID start_byte:bit_length Имя (единицы) scale offset [lut=N]`

### `ls`

```
  CONFIG.INI       423
  06034200.MLG   24181
  06035700.MLG  118472
12 files
```

### `get <file>` — скачивание файла

Для скачивания MLG файлов без извлечения SD:

```bash
python3 firmware/scripts/usb_get.py /dev/cu.usbmodemXXXX 06034200.MLG
```

Можно указать путь сохранения:

```bash
python3 firmware/scripts/usb_get.py /dev/cu.usbmodemXXXX 06034200.MLG ./downloads/log.mlg
```

Зависимости: `pip3 install pyserial`

Протокол: `FILE:<name>:<size>\n` → raw binary → `\nEND\n`

## Автоматизация (без picocom)

Отправка команд из скрипта через picocom stdin:

```bash
# Одна команда с таймаутом
{ sleep 1; printf "status\r"; sleep 2; printf "\x01\x18"; } | \
  picocom -b 115200 --noreset --quiet /dev/cu.usbmodemXXXX

# Несколько команд подряд
{ sleep 1; printf "help\r"; sleep 2; printf "ls\r"; sleep 2; printf "\x01\x18"; } | \
  picocom -b 115200 --noreset --quiet /dev/cu.usbmodemXXXX
```

- `sleep 1` — ждём подключения CDC
- `printf "cmd\r"` — отправляем команду (`\r` = Enter)
- `sleep 2` — ждём ответ
- `printf "\x01\x18"` — `Ctrl-A Ctrl-X`, выход из picocom

## USB параметры

- USB OTG FS, Device Only, Full Speed 12 Mbit/s
- VID/PID: 0x0483/0x5740 (STMicroelectronics CDC)
- Product: "CANLogger Debug Port"
- VBUS sensing: отключен
- IRQ приоритет: 7 (ниже CAN = 5)
- TX буфер: 640 байт, flush по `\n` или при заполнении
- RX буфер: 80 байт (максимальная длина команды)
- При BUSY: команды ждут до 50ms, stream-вывод молча дропается

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
- `__io_putchar()` → буферизованный `CDC_Transmit_FS()` с retry при BUSY (для команд)
- `debug_cmd_receive()` — вызывается из `CDC_Receive_FS` (ISR), заполняет RX буфер, эхо
- `debug_cmd_poll()` — вызывается из main loop, парсит и выполняет команды
- `debug_out_tick()` — периодический вывод (когда stream включён)
- `debug_out_set_can()` — захват любого CAN фрейма для отображения

Main.c:
- `debug_out_tick(...)` в main loop
- `debug_cmd_poll(&config, init_ok, &can_rx_buf)` в main loop
- `debug_out_set_can(...)` при получении каждого CAN фрейма
