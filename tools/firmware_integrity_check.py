#!/usr/bin/env python3
"""Transfer the fixed corrupted image and check bootloader rejection and FW1 return.

Run: python tools/firmware_integrity_check.py --port /dev/ttyUSB0
Requires a valid application in FW1 and a bootloader with diagnostic logs.
"""

import argparse
import math
from pathlib import Path
import sys
import time

from failsafe_update import BootMonitor
from pack_firmware import BANK_SIZE, HEADER_SIZE
from uart_dfu import CHUNK_SIZE, PAGE_SIZE, exchange, select_update_mode, send

IMAGE = Path(__file__).resolve().parents[1] / "tests/data/firmware_corrupted_1bit.bin"


def corrupted_requests(image):
    """Send the existing file unchanged, including its original expected MD5."""
    yield "AT+DFUOP=2", "open FW2"
    pages = (len(image) + PAGE_SIZE - 1) // PAGE_SIZE
    for page in [*range(1, pages), 0]:  # Commit the header page last.
        data = image[page * PAGE_SIZE:(page + 1) * PAGE_SIZE].ljust(PAGE_SIZE, b"\xff")
        for start in range(0, PAGE_SIZE, CHUNK_SIZE):
            encoded = data[start:start + CHUNK_SIZE].hex().upper()
            yield f"AT+DFUWR={len(encoded)},{encoded}", f"FW2 page {page} data"
        yield f"AT+DFUWR={page}", f"commit FW2 page {page}"
    yield "AT+DFUCL", "close FW2"


def observe_boot(port, timeout, report):
    monitor = BootMonitor(port, lambda message, received=None: report(message))
    send(port, "AT+RESET")
    deadline = time.monotonic() + timeout
    lines = []
    while True:
        event = monitor.read_until(deadline)
        if event is None:
            break
        lines.append(event[1])

    for line in lines:
        if line.startswith(b"BL SWAP "):
            raise RuntimeError("firmware swapping was initiated after uploading corrupted FW2")
        if line == b"BL FW2 VALID":
            raise RuntimeError("bootloader accepted corrupted FW2")
        if line.startswith(b"BL ERROR ") or line in (
                b"BL FW1 INVALID", b"BL BOOT MODE", b"BL NO VALID APPLICATION", b"FW confirmation failed"):
            raise RuntimeError(f"boot failed: {line.decode('ascii', errors='replace')}")
    required = (b"BL FW2 INVALID", b"BL RETURN FW1", b"BL FW1 VALID")
    running = any(line.startswith(b"FW ") and line.endswith(b" running") for line in lines)
    if not all(line in lines for line in required) or not running:
        raise TimeoutError("missing logs confirming FW2 INVALID and return to running FW1")
    if lines.count(b"BL START") != 1:
        raise RuntimeError("expected one boot; unexpected restart or missing BL START")


def run_test(port, image, args, report=print):
    time.sleep(1.5)  # Allow the application's startup confirmation to finish.
    port.reset_input_buffer()
    select_update_mode(port, args.timeout, report)
    send(port, "AT+RESET")
    time.sleep(1.5)
    port.reset_input_buffer()
    exchange(port, "AT", args.timeout)

    report(f"Uploading {len(image)} bytes from {IMAGE}")
    for command, context in corrupted_requests(image):
        exchange(port, command, args.timeout, context=context, report=report)
        if context.startswith("commit "):
            report(f"Upload: {context} acknowledged")
    report("Transfer complete; requesting activation for bootloader integrity validation")
    exchange(port, "AT+RAA=2", args.timeout)
    observe_boot(port, args.boot_timeout, report)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", required=True, help="UART port, e.g. /dev/ttyUSB0")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--timeout", type=float, default=5.0)
    parser.add_argument("--boot-timeout", type=float, default=5.0)
    args = parser.parse_args(argv)
    if args.baud <= 0 or any(not math.isfinite(t) or t <= 0 for t in (args.timeout, args.boot_timeout)):
        parser.error("baud and timeouts must be finite and positive")
    try:
        image = IMAGE.read_bytes()
        if not HEADER_SIZE + 8 <= len(image) <= BANK_SIZE:
            raise ValueError("test image size must fit FW2 and include its header and vector table")
        import serial
        with serial.Serial(args.port, args.baud, timeout=0.01, write_timeout=args.timeout) as port:
            run_test(port, image, args)
        print("PASS: corrupted FW2 rejected; returned to running FW1 without swapping")
        return 0
    except (OSError, ValueError, RuntimeError, ImportError) as error:
        print(f"FAIL: {error}", file=sys.stderr)
        return 1
    except KeyboardInterrupt:
        print("ABORTED", file=sys.stderr)
        return 130


if __name__ == "__main__":
    sys.exit(main())
