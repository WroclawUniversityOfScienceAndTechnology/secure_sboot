#!/usr/bin/env python3
"""Build the legacy 512-byte header + firmware payload image (little endian)."""
import argparse
import hashlib
from pathlib import Path
import struct
from firmware_header import FirmwareHeader, FwHeaderConstants, ImageType

HEADER_SIZE = FwHeaderConstants.HEADER_SIZE
BANK_SIZE = 0x5800
APP_ADDRESS = 0x08005200
MAGIC = FwHeaderConstants.IMAGE_MAGIC


def calculate_hash(payload, magic):
    """Same magic-seeded MD5 calculation as the original patch_image.py."""
    hasher = hashlib.md5()
    hasher.update(magic.to_bytes(2, "little"))
    hasher.update(payload)
    return hasher.digest()


def pack(payload, version, git_sha=b"\0" * 8):
    if not 8 <= len(payload) <= BANK_SIZE - HEADER_SIZE:
        raise ValueError("payload must be 8..22016 bytes")
    stack, reset = struct.unpack_from("<II", payload)
    if not (0x20000000 < stack <= 0x20005000 and stack % 8 == 0):
        raise ValueError("invalid initial stack pointer")
    if not (reset & 1 and APP_ADDRESS <= (reset & ~1) < APP_ADDRESS + len(payload)):
        raise ValueError("payload must be linked to execute at 0x08005200")
    if len(version) != 3 or any(not 0 <= n <= 255 for n in version):
        raise ValueError("version must have three components in 0..255")
    if len(git_sha) != 8:
        raise ValueError("git SHA prefix must contain exactly 8 bytes")
    header = FirmwareHeader()
    header.data_length = len(payload)
    header.image_magic = MAGIC
    header.md5_hash = calculate_hash(payload, MAGIC)
    header.image_type = ImageType.Firmware
    header.version = ".".join(map(str, version))
    header.vector_address = 4
    header.git_sha = git_sha
    return header.to_bytes() + payload


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("input", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--version", required=True, help="major.minor.patch")
    parser.add_argument("--git-sha", default="00" * 8, help="16 hex digits")
    parser.add_argument("--reserved-header", action="store_true",
                        help="input already has the linker's 512-byte header reservation")
    parser.add_argument("--header-output", type=Path,
                        help="also write the header for objcopy --update-section")
    args = parser.parse_args()
    try:
        if args.input.resolve() == args.output.resolve():
            raise ValueError("input and output must be different files")
        if args.header_output and args.header_output.resolve() in (args.input.resolve(), args.output.resolve()):
            raise ValueError("header output must be a separate file")
        payload = args.input.read_bytes()
        if args.reserved_header:
            payload = payload[HEADER_SIZE:]
        image = pack(payload, tuple(map(int, args.version.split("."))),
                     bytes.fromhex(args.git_sha))
        args.output.write_bytes(image)
        if args.header_output:
            args.header_output.write_bytes(image[:HEADER_SIZE])
    except (ValueError, OSError) as error:
        parser.error(str(error))


if __name__ == "__main__":
    main()
