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
| `put <f> N` | Загрузить N байт в файл на SD |
| `fault` | Симулировать fatal error (записать FAULT файл) |
| `stop` | Безопасно закрыть SD (перед прошивкой) |
| `settime YYYY-MM-DD HH:MM:SS` | Установить RTC (переживёт reset пока VBAT жив) |
| `lastfault` | Последний fault из BKP регистров + session counter |

### `help`

```
Commands:
  help      - this message
  status    - system status
  stream    - toggle periodic output
  config    - show loaded config
  ls        - list MLG files on SD
  get <f>   - download file (use usb_get.py)
  put <f> N - upload N bytes to file
  fault     - simulate fatal error, write FAULT file
  stop      - close SD safely (before flash)
  settime YYYY-MM-DD HH:MM:SS - set RTC (survives reset via VBAT)
  lastfault - show last fault from BKP regs (persistent)
```

### `status`

```
uptime=316s frames=1125808 fields=64 init=1 stream=0
file=10012900.MLG size=9363600 files=1 blocks=218 err=0/0
sdw: tot=7461 lat=12/86 scratch=1548
sdst: calls=13180 fail=6 rescued=6 hard=0 maxret=4ms last_raw=0
rb: count=0 head=432 tail=432
sd: 23789888KB free / 31150208KB total
can: 32 ids bus=active tec=0 rec=0 lec=none overrun=0
  0xD00[8] 1ms ago: 28 10 C7 13 4C 04 5A 14
  ...
```

Дополнительные строки появляются по ситуации:

- `sdio: cb=... ctmo=... dtmo=... dcrc=... dma=... last=... hal=...` — показывается только если хотя бы раз сработал `HAL_SD_ErrorCallback` или `hsd.ErrorCode != 0`.
- `sd_w: nr=... c13e=... c13t=... dma=... cmd=... oob=...` — fine-grained счётчики early-return путей в `BSP_SD_WriteBlocks_DMA`, показываются только если их сумма > 0.
- `sdw_err: eb=... dma=... txto=... csto=... sdma=... stxto=... @sec=... cnt=... tick=...` — per-failure-point счётчики wrapper'а `SD_write` (4 fast-path + 2 scratch-path точки отказа), показывается только если была хотя бы одна ошибка.
- `rec=N lastrec=FR_X@site` — появляется рядом с `err=` если срабатывал `recover_file()`. `FR_X` — код FatFS, `site` = `sync` / `write`.

| Строка | Описание |
|--------|----------|
| `uptime` | Секунды с момента старта |
| `frames` | Количество обработанных CAN-фреймов |
| `fields` | Количество полей в конфиге |
| `init` | 1 = конфиг загружен, логирование идёт; 0 = ошибка |
| `stream` | 1 = периодический вывод включён |
| `file` | Текущий MLG файл, размер в байтах |
| `files` | Сколько файлов создано с момента старта |
| `blocks` | Количество data blocks в текущем файле (uint8, wraps @256) |
| `err=N/S` | N = счётчик ошибок SD, S = error state (1 = фатальная ошибка) |
| `rec` | Сколько раз `recover_file()` отработал успешно (должно быть 0 на стабильной системе) |
| `lastrec` | FRESULT и site последнего recovery trigger (диагностика) |
| `sdw` | SD_write wrapper: `tot` = всего вызовов, `lat` = last/max latency (мс), `scratch` = сколько раз попали в slow path (unaligned buffer) |
| `sdst` | SD_status wrapper: `calls` = всего вызовов, `fail` = transient PROGRAMMING событий, `rescued` = вылечены retry loop'ом, `hard` = превысили SD_STATUS_RETRY_MS, `maxret` = максимальное время retry (мс), `last_raw` = HAL card state на последнем hard fail |
| `rb` | Ring buffer: count = фреймов в очереди, head/tail = позиции |
| `sd` | Свободное / общее место на SD карте |
| `can` | CAN диагностика: кол-во ID, `bus` = active/passive/bus-off, `tec`/`rec` = error counters, `lec` = last error code (none/stuff/form/ack/bit_rec/bit_dom/crc), `overrun` = FIFO0 overflow |
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
- `debug_cmd_poll()` — вызывается из task_producer, парсит и выполняет команды
- `debug_out_tick()` — периодический вывод (когда stream включён)
- `debug_out_set_can()` — захват любого CAN фрейма для отображения

Main.c (task_producer — StartDefaultTask):
- `debug_out_tick(...)` в task_producer loop
- `debug_cmd_poll(&config, init_ok, &can_rx_buf)` в task_producer loop
- `debug_out_set_can(...)` при получении каждого CAN фрейма
