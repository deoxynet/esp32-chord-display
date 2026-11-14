#!/usr/bin/env python3
"""Capture Core2 diagnostics and optionally send its supported serial commands.

Run with PlatformIO's Python environment (it includes pyserial), or install
pyserial in a local virtual environment. Opening a USB port can reset the Core2;
commands are delayed until startup has completed.
"""

import argparse
import os
from pathlib import Path
import select
import signal
import sys
import time

import serial


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("port")
    parser.add_argument("--duration", type=float, default=30)
    parser.add_argument("--command", default="?")
    parser.add_argument("--log", type=Path)
    args = parser.parse_args()

    port = serial.Serial(port=None, baudrate=115200, timeout=0.2)
    port.dtr = False
    port.rts = False
    port.port = args.port
    captured = bytearray()
    running = True

    def stop_capture(_signal, _frame):
        nonlocal running
        running = False

    signal.signal(signal.SIGINT, stop_capture)
    signal.signal(signal.SIGTERM, stop_capture)
    log = None
    if args.log:
        args.log.parent.mkdir(parents=True, exist_ok=True)
        log = args.log.open("wb")
    with port:
        start = time.monotonic()
        sent = False
        listen_stdin = True
        while running and time.monotonic() - start < args.duration:
            data = port.read(4096)
            if data:
                captured.extend(data)
                sys.stdout.write(data.decode(errors="replace"))
                sys.stdout.flush()
                if log:
                    log.write(data)
                    log.flush()
            if not sent and (b"Serial:" in captured or time.monotonic() - start >= 3):
                port.write(args.command.encode("ascii"))
                sent = True
            if listen_stdin and select.select([sys.stdin], [], [], 0)[0]:
                command = os.read(sys.stdin.fileno(), 4096)
                if command:
                    port.write(command)
                else:
                    listen_stdin = False
    if log:
        log.close()


if __name__ == "__main__":
    main()
