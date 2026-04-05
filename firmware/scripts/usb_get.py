#!/usr/bin/env python3
"""Download a file from CANLogger SD card via USB CDC.

Usage: python3 usb_get.py <serial_port> <filename> [output_path]

Example:
    python3 usb_get.py /dev/cu.usbmodem1234 06041200.MLG
    python3 usb_get.py /dev/cu.usbmodem1234 06041200.MLG ./downloads/log.mlg
"""

import sys
import serial
import time


def main():
    if len(sys.argv) < 3:
        print(__doc__.strip())
        sys.exit(1)

    port = sys.argv[1]
    filename = sys.argv[2]
    output = sys.argv[3] if len(sys.argv) > 3 else filename

    ser = serial.Serial(port, 115200, timeout=5)
    time.sleep(0.1)

    # Drain any pending output
    ser.reset_input_buffer()

    # Send get command
    ser.write(f"get {filename}\n".encode())
    ser.flush()

    # Read lines until we find FILE: or ERR: header (skip echo)
    header = None
    for _ in range(10):
        line = ser.readline().decode(errors="replace").strip()
        if not line:
            continue
        # Strip echo prefix: find FILE: or ERR: anywhere in line
        for prefix in ("FILE:", "ERR:"):
            idx = line.find(prefix)
            if idx >= 0:
                header = line[idx:]
                break
        if header:
            break

    if header is None:
        print("No response from device", file=sys.stderr)
        ser.close()
        sys.exit(1)

    if header.startswith("ERR:"):
        print(f"Device error: {header}", file=sys.stderr)
        ser.close()
        sys.exit(1)

    parts = header.split(":")
    if len(parts) < 3:
        print(f"Bad header: {header}", file=sys.stderr)
        ser.close()
        sys.exit(1)

    size = int(parts[2])
    print(f"Downloading {parts[1]} ({size} bytes)...")

    # Read raw file data
    received = 0
    with open(output, "wb") as f:
        while received < size:
            chunk = ser.read(min(4096, size - received))
            if not chunk:
                print(f"\nTimeout after {received}/{size} bytes", file=sys.stderr)
                ser.close()
                sys.exit(1)
            f.write(chunk)
            received += len(chunk)
            pct = received * 100 // size
            print(f"\r  {received}/{size} ({pct}%)", end="", flush=True)

    print()

    # Read END marker
    end = ser.readline().decode(errors="replace").strip()
    # May get empty line before END
    if not end:
        end = ser.readline().decode(errors="replace").strip()

    ser.close()

    if end == "END":
        print(f"Saved to {output}")
    else:
        print(f"Warning: expected END marker, got: {end!r}", file=sys.stderr)
        print(f"File saved to {output} (may be incomplete)")


if __name__ == "__main__":
    main()
