#!/usr/bin/env python3
"""Upload a packed image to FW using the secondary bootloader's UART AT protocol."""
import argparse
from pathlib import Path
import struct
import sys
import time

from pack_firmware import HEADER_SIZE, pack

PAGE_SIZE = 1024
CHUNK_SIZE = 256  # 512 hex characters; fits the 1088-byte AT frame limit.
# These archived payloads predate nv_journal.c. Their application-side NV writes
# erase journal page A and can resurrect an unfinished swap on the next reset.
LEGACY_NV_DIGESTS = {
    bytes.fromhex("afbf036e297814d7b6ae8166db326ac3"),  # tests/data/firmware1.bin
    bytes.fromhex("f5e24dc4b3795b650b15ec8429138565"),  # tests/data/firmware2.bin
}


class DeviceRejected(RuntimeError):
    """The MCU explicitly replied ERROR, rather than timing out."""


def check_nv_compatibility(image):
    """Block known obsolete test applications; this is not a general ABI check."""
    if image[8:24] in LEGACY_NV_DIGESTS:
        raise ValueError("known legacy firmware uses the old NV format and can corrupt the swap journal. "
                         "Rebuild with '.venv/bin/pwrforge build -p Firmware' and use "
                         "build/stm32/Firmware/bin/firmware.bin; tests/data/firmware1.bin and "
                         "firmware2.bin are archival images, unsuitable for this bootloader")


def validate_image(image):
    if len(image) < HEADER_SIZE + 8:
        raise ValueError("image is missing its header or vector table")
    fields = struct.unpack_from("<II16sHBBBBII8s", image)
    expected = pack(image[HEADER_SIZE:], fields[5:8], fields[10])
    if image != expected:
        raise ValueError("invalid firmware header, length or checksum; use the packaged firmware.bin")


def dfu_requests(image):
    """Commands and transfer locations, including the header-last page order."""
    validate_image(image)
    yield "AT+DFUOP=2", "open FW2"
    pages = (len(image) + PAGE_SIZE - 1) // PAGE_SIZE
    # Commit the new header last. The final partial page is padded with erased bytes.
    for page in [*range(1, pages), 0]:
        data = image[page * PAGE_SIZE:(page + 1) * PAGE_SIZE].ljust(PAGE_SIZE, b"\xff")
        for start in range(0, PAGE_SIZE, CHUNK_SIZE):
            encoded = data[start:start + CHUNK_SIZE].hex().upper()
            yield (f"AT+DFUWR={len(encoded)},{encoded}",
                   f"FW2 page {page}, chunk {start // CHUNK_SIZE + 1}/{PAGE_SIZE // CHUNK_SIZE}")
        yield f"AT+DFUWR={page}", f"commit FW2 page {page}"
    yield "AT+DFUCL", "close FW2"


def dfu_commands(image):
    for command, _ in dfu_requests(image):
        yield command


def send(port, command):
    frame = (command + "\r\n").encode("ascii")
    if port.write(frame) != len(frame):
        raise OSError("short UART write")
    port.flush()


def exchange(port, command, timeout, context=None, report=None):
    send(port, command)
    deadline = time.monotonic() + timeout
    label = (command if command.startswith("AT+RAA=") else command.split("=")[0])
    label += f" ({context})" if context else ""
    pending = bytearray()
    received_tail = bytearray()
    previous_timeout = port.timeout
    try:
        while True:
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                break
            # UART/USB can split even OK into separate reads. Keep fragments
            # until LF, and bound each read by the complete command deadline.
            port.timeout = min(remaining, 0.05)
            data = port.read(min(port.in_waiting or 1, 4096))
            received_tail.extend(data)
            del received_tail[:-160]
            pending.extend(data)
            while b"\n" in pending:
                line, _, rest = pending.partition(b"\n")
                pending = bytearray(rest)
                response = bytes(line).strip(b"\x00 \t\r")
                if report and response not in (b"", b"OK"):
                    report("RX " + response.decode("ascii", errors="backslashreplace"))
                if response == b"OK":
                    return
                if response == b"ERROR":
                    raise DeviceRejected(f"device rejected {label}")
            if len(pending) > 4096:
                raise RuntimeError(f"unterminated UART response to {label}; RX tail={bytes(received_tail)!r}")
    finally:
        port.timeout = previous_timeout
    detail = f"RX tail={bytes(received_tail)!r}" if received_tail else "no UART bytes received"
    raise TimeoutError(f"no OK reply to {label}; {detail}")


def capture_recovery(port, timeout, report):
    """Keep MCU output after a recovery reset instead of discarding the reason."""
    deadline = time.monotonic() + max(1.5, timeout)
    pending = bytearray()
    previous_timeout = port.timeout
    try:
        while time.monotonic() < deadline:
            port.timeout = min(0.05, max(0, deadline - time.monotonic()))
            pending.extend(port.read(min(port.in_waiting or 1, 4096)))
            while b"\n" in pending:
                line, _, rest = pending.partition(b"\n")
                pending = bytearray(rest)
                line = bytes(line).strip(b"\x00 \t\r")
                if line:
                    report("RECOVERY RX " + line.decode("ascii", errors="backslashreplace"))
            if len(pending) > 4096:
                raise RuntimeError("unterminated recovery output exceeds 4096 bytes; check UART wiring/baud")
        if pending:
            report(f"RECOVERY RX partial: {bytes(pending)!r}")
    finally:
        port.timeout = previous_timeout


def select_update_mode(port, timeout, report=None):
    """Select boot mode, allowing one reboot to finish a pending swap first.

    Only retry an explicit rejection of the mode selection. Never retry DFU
    data writes: the page buffer appends bytes and a duplicate could corrupt it.
    """
    report = report or print
    exchange(port, "AT", timeout, context="before entering boot mode")
    try:
        exchange(port, "AT+RAA=11", timeout, context="select update mode before writing FW2")
    except DeviceRejected:
        report("AT+RAA=11 rejected before upload; resetting once to allow pending swap recovery. "
               "NV state or flash-write failures can also cause this rejection.")
        send(port, "AT+RESET")
        capture_recovery(port, timeout, report)
        exchange(port, "AT", timeout, context="after recovery reset")
        try:
            exchange(port, "AT+RAA=11", timeout, context="select update mode after recovery reset")
        except DeviceRejected as error:
            raise RuntimeError("AT+RAA=11 still rejected after one recovery reset; firmware transfer "
                               "has not started. Check RECOVERY RX logs: a pending/failed swap, "
                               "unreadable NV journal, or failed NV flash write prevents updates. "
                               "No firmware pages were written by this upload.") from error


def upload(port, image, activate, timeout, report=None):
    validate_image(image)
    check_nv_compatibility(image)
    # Persist recovery mode before modifying FW2, including when called from FW1.
    select_update_mode(port, timeout, report)
    send(port, "AT+RESET")  # RESET has no acknowledgement.
    time.sleep(1.5)
    port.reset_input_buffer()
    exchange(port, "AT", timeout, context="after reset into boot mode")
    for command, context in dfu_requests(image):
        # DFU data appends to the device's page buffer. A lost reply must stop
        # the transfer: retrying that chunk could append the same bytes twice.
        exchange(port, command, timeout, context=context, report=report)
        if report and context.startswith("commit "):
            report(f"Upload: {context} acknowledged")
    if activate:
        exchange(port, "AT+RAA=2", timeout)
        send(port, "AT+RESET")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("image", type=Path)
    parser.add_argument("--port", help="serial port, e.g. /dev/ttyUSB0")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--timeout", type=float, default=5.0)
    parser.add_argument("--activate", action="store_true", help="select FW2 and reset after upload")
    parser.add_argument("--dry-run", action="store_true", help="validate and print DFU commands without opening UART")
    args = parser.parse_args()
    if args.timeout <= 0 or args.baud <= 0:
        parser.error("baud and timeout must be positive")
    try:
        image = args.image.read_bytes()
        validate_image(image)
        check_nv_compatibility(image)
        if args.dry_run:
            print("\n".join(dfu_commands(image)))
            return
        if not args.port:
            parser.error("--port is required unless --dry-run is used")
        try:
            import serial
        except ImportError:
            parser.error("pyserial is required; use the Python environment containing pwrforge")
        with serial.Serial(args.port, args.baud, timeout=0.2, write_timeout=args.timeout) as port:
            # Allow the test firmware's one-time startup confirmation to finish.
            time.sleep(1.5)
            port.reset_input_buffer()
            upload(port, image, args.activate, args.timeout, report=print)
        print(f"Uploaded {len(image)} bytes to FW2. " +
              ("Activation requested; check the firmware startup banner." if args.activate else
               "Device remains in boot mode; use AT+RAA=2 then AT+RESET to activate."))
    except (OSError, ValueError, RuntimeError, TimeoutError) as error:
        print(f"UART upload failed: {error}", file=sys.stderr)
        sys.exit(1)


if __name__ == "__main__":
    main()
