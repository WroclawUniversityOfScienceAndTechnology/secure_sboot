#!/usr/bin/env python3
"""Dependency-free Linux host regression tests; no device or Docker required."""
from pathlib import Path
import importlib.util
import struct
import shutil
import os
import subprocess
import tempfile
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
spec = importlib.util.spec_from_file_location("packer", ROOT / "tools/pack_firmware.py")
packer = importlib.util.module_from_spec(spec)
spec.loader.exec_module(packer)
from uart_dfu import dfu_commands

with tempfile.TemporaryDirectory(prefix="sboot-test-") as directory:
    tmp = Path(directory)
    # Run the real memory-map generator; avoid copying constants into a mock header.
    project = tmp / "project"
    project.mkdir()
    (project / "CMakeLists.txt").write_text(
        'cmake_minimum_required(VERSION 3.16)\nproject(host NONE)\n'
        f'add_subdirectory("{ROOT}/src/gen/memory_map" memory_map)\n')
    subprocess.run([shutil.which("cmake") or str(ROOT / ".venv/bin/cmake"), "-S", str(project), "-B", str(tmp / "build")], check=True)
    # Link the firmware template with a tiny native object to check the reserved
    # header and vector placement independently of the ARM toolchain.
    assembly = tmp / "layout.s"
    assembly.write_text('.section .isr_vector,"a"\n.global g_pfnVectors\n'
                        'g_pfnVectors: .long 0x20005000, 0x08005209\n'
                        '.text\n.global Reset_Handler\nReset_Handler: ret\n')
    subprocess.run(["as", "--32", str(assembly), "-o", str(tmp / "layout.o")], check=True)
    subprocess.run(["ld", "-m", "elf_i386", "-T",
                    str(tmp / "build/memory_map/FirmwareFW1.ld"),
                    str(tmp / "layout.o"), "-o", str(tmp / "layout.elf")], check=True)
    symbols = subprocess.check_output(["nm", str(tmp / "layout.elf")], text=True)
    assert "08005200 R g_pfnVectors" in symbols, symbols
    files = []
    for version in (1, 2):
        payload = struct.pack("<II", 0x20005000, 0x08005209) + bytes([version]) * 1016
        image = packer.pack(payload, (version, 0, 0))
        path = tmp / f"fw{version}.bin"
        path.write_bytes(image)
        files.append(str(path))
    for payload in (b"", b"\0" * 8, b"x" * 22017):
        try:
            packer.pack(payload, (1, 0, 0))
        except ValueError:
            pass
        else:
            raise AssertionError("invalid payload accepted")
    libs = ROOT / "src/libs"
    sources = ["bootloader_interface/swap.c", "firmware_details/firmware_image.c",
               "firmware_details/firmware_details.c", "non_volatile_items/non_volatile_items.c",
               "non_volatile_items/nv_journal.c", "bootloader_interface/boot_selection.c",
               "validity/validity.c", "validity/md5.c", "commands_handler/commands/cmd_dfu.c",
               "commands_handler/commands/params_parser.c",
               "commands_handler/commands/simple_output_builder.c"]
    includes = ["bootloader_interface", "firmware_details", "non_volatile_items",
                "flash_driver", "validity", "commands_handler"]
    command = ["gcc", "-std=c11", "-g", "-O1", "-fsanitize=undefined",
               "-Werror=implicit-function-declaration", "-Wno-int-to-pointer-cast",
               "-I" + str(tmp / "build/memory_map")]
    command += ["-I" + str(libs / name) for name in includes]
    command += [str(ROOT / "tests/bootloader/host_test.c")]
    command += [str(ROOT / "tests/bootloader/flash_mock.c")]
    command += [str(libs / name) for name in sources]
    command += ["-o", str(tmp / "host_test")]
    subprocess.run(command, check=True)
    if len(sys.argv) > 1:
        image_path = Path(sys.argv[1]).resolve()
        transcript = tmp / "dfu.txt"
        transcript.write_text("\n".join(dfu_commands(image_path.read_bytes())) + "\n")
        files += [str(image_path), str(transcript)]
    subprocess.run([str(tmp / "host_test"), *files], check=True,
                   env={**os.environ, "UBSAN_OPTIONS": "halt_on_error=1"})
    # Different data in every page: equal erased tails cannot conceal a wrong
    # page index, repeated exchange, or loss of either firmware version.
    power_images = []
    for version in (1, 2):
        payload = struct.pack("<II", 0x20005000, 0x08005209)
        payload += bytes((i * 17 + (i >> 8) * 13 + version * 37) & 255
                         for i in range(0x5800 - 512 - 8))
        path = tmp / f"full-fw{version}.bin"
        path.write_bytes(packer.pack(payload, (version, 0, 0)))
        power_images.append(str(path))
    power_command = [part.replace(str(ROOT / "tests/bootloader/host_test.c"),
                                   str(ROOT / "tests/bootloader/power_fail_test.c"))
                     .replace(str(tmp / "host_test"), str(tmp / "power_fail_test"))
                     for part in command]
    subprocess.run(power_command, check=True)
    subprocess.run([str(tmp / "power_fail_test"), *power_images], check=True,
                   env={**os.environ, "UBSAN_OPTIONS": "halt_on_error=1"})
