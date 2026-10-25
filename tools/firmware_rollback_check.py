#!/usr/bin/env python3
"""Activate the fixed checksum-valid crash image and verify failed-boot rollback.

Run: python tools/firmware_rollback_check.py --port /dev/ttyUSB0
Requires a working, confirming application and bootloader diagnostic logs.
After activation, all resets come from the failing image; no relay is used.
"""

import argparse
import math
from pathlib import Path
import re
import sys
import time

from failsafe_update import BootMonitor
from uart_dfu import check_nv_compatibility, exchange, send, upload, validate_image

IMAGE = Path(__file__).resolve().parents[1] / "tests/data/firmware_corrupted_rollback.bin"
FAILED_BOOT_ATTEMPTS = 3  # MAX_FAILED_BOOT_COUNTS in non_volatile_items.h.
VERIFIED = b"BL FW VERIFIED "


def is_boot_start(line):
    # MCU resets can leave framing bytes (e.g. 0xf8) before the next banner.
    # Match only a complete banner with non-printable leading bytes; arbitrary
    # text or damaged markers must not invent an additional application trial.
    # BootMonitor retains the raw line in the RX log for diagnosis.
    return re.fullmatch(rb"[\x00-\x20\x7f-\xff]*BL START", line) is not None


def image_digest(line):
    if not re.fullmatch(rb"BL FW VERIFIED [0-9a-f]{32}", line):
        raise RuntimeError(f"invalid bootloader checksum log: {line!r}")
    return line[len(VERIFIED):]


def check_error(line):
    if line.startswith(b"BL ERROR ") or line in (
            b"BL BOOT MODE", b"BL NO VALID APPLICATION", b"BL FW1 INVALID",
            b"BL FW2 INVALID", b"BL FW2 REJECTED INTEGRITY", b"BL SWAP ROLLBACK",
            b"FW confirmation failed", b"FW confirmation disabled"):
        raise RuntimeError(f"rollback test failed: {line.decode('ascii', errors='replace')}")


def baseline_boot(port, timeout, report):
    """Identify the working application before touching FW2."""
    monitor = BootMonitor(port, lambda message, received=None: report(message))
    send(port, "AT+RESET")
    deadline = time.monotonic() + timeout
    digest = None
    started = False
    boot_seen = False
    while (event := monitor.read_until(deadline)) is not None:
        line = event[1]
        # FW2 may be empty before the test. Only FW1 must be valid here.
        if line not in (b"BL FW2 INVALID",):
            check_error(line)
        if is_boot_start(line):
            if boot_seen:
                raise RuntimeError("baseline application restarted before confirming")
            boot_seen = True
        elif line.startswith(b"BL SWAP "):
            raise RuntimeError("baseline has a pending swap; recover a working application first")
        elif line.startswith(VERIFIED):
            digest = image_digest(line)
        elif line.startswith(b"FW ") and line.endswith(b" running"):
            if not boot_seen or digest is None or started:
                raise RuntimeError("baseline startup is missing boot identity or restarted")
            started = True
        elif line == b"FW confirmed" and started:
            return digest
    raise TimeoutError("baseline application did not boot with a checksum and confirm startup")


def observe_rollback(port, candidate, baseline, timeout, report):
    monitor = BootMonitor(port, lambda message, received=None: report(message))
    send(port, "AT+RESET")
    deadline = time.monotonic() + timeout
    boots = 0
    swaps = 0
    swap_open = False
    accepted = False
    trials = []
    active = None
    running = False
    while (event := monitor.read_until(deadline)) is not None:
        line = event[1]
        check_error(line)
        if is_boot_start(line):
            if swap_open or running:
                raise RuntimeError("device restarted during a swap or restored application startup")
            boots += 1
            active = None
        elif line == b"BL FW2 VALID" and swaps == 0:
            accepted = True
        elif line == b"BL SWAP START":
            if boots == 0 or swap_open or swaps >= 2:
                raise RuntimeError("unexpected swap sequence")
            if swaps == 0 and not accepted:
                raise RuntimeError("missing FW2 integrity acceptance before activation swap")
            if swaps == 1 and (len(trials) != FAILED_BOOT_ATTEMPTS or boots <= trials[-1]):
                raise RuntimeError("rollback occurred without the expected failed application boots")
            swap_open = True
        elif line == b"BL SWAP DONE":
            if not swap_open:
                raise RuntimeError("swap completed without a start marker")
            swaps += 1
            swap_open = False
        elif line.startswith(VERIFIED):
            if boots == 0 or swap_open:
                raise RuntimeError("application handoff before swap completion")
            active = image_digest(line)
            if active == candidate:
                if swaps != 1 or boots in trials or len(trials) >= FAILED_BOOT_ATTEMPTS:
                    raise RuntimeError("unexpected candidate boot or failed-boot limit exceeded")
                trials.append(boots)
                report(f"Unconfirmed candidate boot {len(trials)}/{FAILED_BOOT_ATTEMPTS}")
            elif active != baseline or swaps != 2 or len(trials) != FAILED_BOOT_ATTEMPTS:
                raise RuntimeError("original application was not restored by failed-boot rollback")
        elif line.startswith(b"FW ") and line.endswith(b" running"):
            if active != baseline or swaps != 2 or running:
                raise RuntimeError("application ran before rollback restored the original image")
            running = True
        elif line == b"FW confirmed":
            if not running or active != baseline:
                raise RuntimeError("confirmation received without restored application startup")
            report("Original firmware checksum restored; application running and confirmed")
            return
    raise TimeoutError(f"rollback did not restore a running, confirmed application within {timeout:g} s "
                       f"(completed swaps={swaps}, candidate boots={len(trials)})")


def run_test(port, image, args, report=print):
    validate_image(image)
    check_nv_compatibility(image)
    candidate = image[8:24].hex().encode("ascii")
    time.sleep(1.5)
    port.reset_input_buffer()
    exchange(port, "AT", args.timeout)
    baseline = baseline_boot(port, args.boot_timeout, report)
    if baseline == candidate:
        raise RuntimeError("rollback fixture is already installed; start with a working application")
    report(f"Baseline firmware checksum: {baseline.decode('ascii')}")
    report(f"Uploading unchanged rollback fixture: {IMAGE} ({len(image)} bytes)")
    upload(port, image, activate=False, timeout=args.timeout, report=report)
    exchange(port, "AT+RAA=2", args.timeout)
    report("Activating crash image; waiting for autonomous resets and rollback")
    observe_rollback(port, candidate, baseline, args.boot_timeout, report)
    exchange(port, "AT", args.timeout, context="restored application responsiveness")


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", help="UART port, e.g. /dev/ttyUSB0")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--timeout", type=float, default=5.0)
    parser.add_argument("--boot-timeout", type=float, default=30.0,
                        help="total observation window for activation, failed boots and rollback (default: 30)")
    parser.add_argument("--dry-run", action="store_true", help="validate the static fixture without hardware")
    args = parser.parse_args(argv)
    if not args.port and not args.dry_run:
        parser.error("--port is required unless --dry-run is used")
    if args.baud <= 0 or any(not math.isfinite(t) or t <= 0 for t in (args.timeout, args.boot_timeout)):
        parser.error("baud and timeouts must be finite and positive")
    try:
        image = IMAGE.read_bytes()
        validate_image(image)
        check_nv_compatibility(image)
        if args.dry_run:
            print(f"DRY RUN: {IMAGE}; {len(image)} bytes; valid MD5 {image[8:24].hex()}; no hardware accessed")
            return 0
        import serial
        with serial.Serial(args.port, args.baud, timeout=0.01, write_timeout=args.timeout) as port:
            run_test(port, image, args)
        print("PASS: checksum-valid faulty FW2 swapped in, failed boot trials, and automatically "
              "rolled back to the original running, confirmed application")
        return 0
    except (OSError, ValueError, RuntimeError, ImportError) as error:
        print(f"FAIL: {error}", file=sys.stderr)
        return 1
    except KeyboardInterrupt:
        print("ABORTED", file=sys.stderr)
        return 130


if __name__ == "__main__":
    sys.exit(main())
