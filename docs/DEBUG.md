# Debug: USB CDC

## Connection

Micro-USB cable from the board to a PC. On macOS the device appears as `/dev/cu.usbmodemXXXX`.

```bash
# Find the port
ls /dev/cu.usbmodem*

# Connect (picocom is recommended)
picocom /dev/cu.usbmodemXXXX -b 115200

# Exit picocom: Ctrl-A Ctrl-X
```

Install: `brew install picocom`

## CLI commands

On connect the device is silent (the stream is off). Type commands, press Enter to submit. Input is echoed.

| Command | Description |
|---------|-------------|
| `help` | Command list |
| `status` | One-shot status snapshot |
| `stream` | Enable periodic output (once per second). Any input disables the stream |
| `config` | Show the loaded config |
| `ls` | List files on the SD with sizes |
| `get <f>` | Download a file from SD (use `usb_get.py`). Auto-pauses the logger; reboot to resume recording |
| `put <f> N` | Upload N bytes into a file on SD. Auto-pauses the logger; reboot to apply a new config |
| `fault` | Simulate a fatal error (writes a FAULT file) |
| `stop` | Close the SD safely (before flashing) |
| `pause` | Close the log file, keep the SD mounted (reboot to resume recording) |
| `mark [txt]` | Write a marker into the log (MLG native block type 0x01). Without an argument — `cdc`. Also triggered by the K0 button (msg=`btn`) |
| `settime YYYY-MM-DD HH:MM:SS` | Set the RTC (survives reset while VBAT is alive) |
| `lastfault` | Last fault from the BKP registers + session counter |

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
  pause     - flush & close log, keep SD mounted (reboot to resume)
  mark [txt]- write marker to log (K0 button or CDC, txt default 'cdc')
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

Extra lines show up conditionally:

- `sdio: cb=... ctmo=... dtmo=... dcrc=... dma=... last=... hal=...` — shown only if `HAL_SD_ErrorCallback` has fired at least once or `hsd.ErrorCode != 0`.
- `sd_w: nr=... c13e=... c13t=... dma=... cmd=... oob=...` — fine-grained counters of the early-return paths in `BSP_SD_WriteBlocks_DMA`; shown only if their sum is > 0.
- `sdw_err: eb=... dma=... txto=... csto=... sdma=... stxto=... @sec=... cnt=... tick=...` — per-failure-point counters in the `SD_write` wrapper (4 fast-path + 2 scratch-path failure sites); shown only if at least one error has occurred.
- `rec=N lastrec=FR_X@site` — appears next to `err=` if `recover_file()` fired. `FR_X` is the FatFS code, `site` = `sync` / `write`.

| Line | Description |
|------|-------------|
| `uptime` | Seconds since start |
| `frames` | CAN frames processed |
| `fields` | Number of fields in the config |
| `init` | 1 = config loaded, logging active; 0 = error |
| `stream` | 1 = periodic output enabled |
| `file` | Current MLG file, size in bytes |
| `files` | Files created since start |
| `blocks` | Data blocks in the current file (uint8, wraps at 256) |
| `err=N/S` | N = SD error count, S = error state (1 = fatal error) |
| `rec` | How many times `recover_file()` succeeded (should be 0 on a stable system) |
| `lastrec` | FRESULT and site of the last recovery trigger (diagnostics) |
| `sdw` | SD_write wrapper: `tot` = total calls, `lat` = last/max latency (ms), `scratch` = how many times the slow path was hit (unaligned buffer) |
| `sdst` | SD_status wrapper: `calls` = total calls, `fail` = transient PROGRAMMING events, `rescued` = recovered by the retry loop, `hard` = exceeded SD_STATUS_RETRY_MS, `maxret` = max retry time (ms), `last_raw` = HAL card state on the last hard fail |
| `rb` | Ring buffer: count = frames queued, head/tail = positions |
| `sd` | Free / total space on the SD card |
| `can` | CAN diagnostics: ID count, `bus` = active/passive/bus-off, `tec`/`rec` = error counters, `lec` = last error code (none/stuff/form/ack/bit_rec/bit_dom/crc), `overrun` = FIFO0 overflow |
| `0x640[8]` | CAN ID, DLC, raw bytes, time since the last update |

### `stream`

Enables periodic output (like `status`, once per second). Any key press turns the stream off — you can immediately type the next command.

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

Format: `[index] CAN_ID start_byte:bit_length Name (units) scale offset [lut=N]`

### `ls`

```
  CONFIG.INI       423
  06034200.MLG   24181
  06035700.MLG  118472
12 files
```

### `get <file>` — downloading a file

To download MLG files without pulling the SD:

```bash
python3 firmware/scripts/usb_get.py /dev/cu.usbmodemXXXX 06034200.MLG
```

A target path can be specified:

```bash
python3 firmware/scripts/usb_get.py /dev/cu.usbmodemXXXX 06034200.MLG ./downloads/log.mlg
```

Dependencies: `pip3 install pyserial`

Protocol: `FILE:<name>:<size>\n` → raw binary → `\nEND\n`

## Automation (without picocom)

Feed commands from a script through picocom stdin:

```bash
# One command with a timeout
{ sleep 1; printf "status\r"; sleep 2; printf "\x01\x18"; } | \
  picocom -b 115200 --noreset --quiet /dev/cu.usbmodemXXXX

# Several commands in sequence
{ sleep 1; printf "help\r"; sleep 2; printf "ls\r"; sleep 2; printf "\x01\x18"; } | \
  picocom -b 115200 --noreset --quiet /dev/cu.usbmodemXXXX
```

- `sleep 1` — wait for the CDC to enumerate
- `printf "cmd\r"` — send a command (`\r` = Enter)
- `sleep 2` — wait for the reply
- `printf "\x01\x18"` — `Ctrl-A Ctrl-X`, exit picocom

## USB parameters

- USB OTG FS, Device Only, Full Speed 12 Mbit/s
- VID/PID: 0x0483/0x5740 (STMicroelectronics CDC)
- Product: "CANLogger Debug Port"
- VBUS sensing: disabled
- IRQ priority: 7 (below CAN = 5)
- TX buffer: 640 bytes, flushed on `\n` or when full
- RX buffer: 80 bytes (maximum command length)
- On BUSY: commands wait up to 50 ms, stream output is silently dropped

## OpenOCD / GDB debugging

All commands run from `firmware/`.

### `make ocd-server`

Starts the OpenOCD GDB server on port :3333. Required for every `ocd-*` command.

```bash
make ocd-server  # in a separate terminal
```

### `make ocd-debug`

Builds, connects via GDB, loads the firmware. Interactive GDB session.

### `make ocd-status`

One-shot device status snapshot (3 seconds of runtime → halt → dump):

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

Extended dump (5 seconds of runtime, `scripts/dump.gdb`).

### `make gdb-server` / `make debug`

Alternative via ST-LINK GDB Server (requires recent ST-Link firmware).

### Important

- OpenOCD and the ST-LINK Programmer cannot run at the same time (both claim SWD)
- Before `make flash`, kill OpenOCD: `pkill -f openocd`

## Architecture

Module `Src/debug_out.c`:
- `__io_putchar()` → buffered `CDC_Transmit_FS()` with retry on BUSY (for commands)
- `debug_cmd_receive()` — called from `CDC_Receive_FS` (ISR), fills the RX buffer, echoes
- `debug_cmd_poll()` — called from task_producer, parses and executes commands
- `debug_out_tick()` — periodic output (when the stream is enabled)
- `debug_out_set_can()` — capture any CAN frame for display

main.c (task_producer — StartDefaultTask):
- `debug_out_tick(...)` in the task_producer loop
- `debug_cmd_poll(&config, init_ok, &can_rx_buf)` in the task_producer loop
- `debug_out_set_can(...)` on every received CAN frame
