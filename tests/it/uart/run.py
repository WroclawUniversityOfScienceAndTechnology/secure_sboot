#!/usr/bin/env python3
"""Run the real UART/parser stack on a simulated HAL, without Docker or hardware."""
from pathlib import Path
import os
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
LIBS = ROOT / "src/libs"
SOURCES = [
    "uart_driver/circularbuffer.c", "uart_driver/circularuart.c",
    "at_comm_parser/data_interface.c", "at_comm_parser/at.c",
    "commands_handler/command_handler.c", "commands_handler/commands/cmd_at.c",
    "commands_handler/commands/simple_output_builder.c",
]
with tempfile.TemporaryDirectory(prefix="sboot-uart-test-") as directory:
    binary = Path(directory) / "uart_test"
    command = [
        "gcc", "-std=c11", "-g", "-Os", "-fstrict-aliasing",
        "-Wall", "-Wextra", "-Werror", "-Wno-unused-parameter",
        "-fsanitize=address,undefined", "-fno-omit-frame-pointer",
        "-fno-pie", "-no-pie", "-I" + str(ROOT / "tests/uart"),
    ]
    command += ["-I" + str(LIBS / name) for name in
                ("uart_driver", "at_comm_parser", "commands_handler")]
    command += [str(ROOT / "tests/uart/host_test.c")]
    command += [str(LIBS / source) for source in SOURCES]
    subprocess.run([*command, "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True, env={
        **os.environ,
        # LeakSanitizer cannot run under ptrace-based development sandboxes.
        "ASAN_OPTIONS": "detect_leaks=0:halt_on_error=1",
        "UBSAN_OPTIONS": "halt_on_error=1",
    })
