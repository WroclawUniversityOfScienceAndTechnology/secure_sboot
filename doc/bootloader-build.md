# STM32F103C8 secondary bootloader

This project builds the secondary bootloader with pwrforge's STM32Cube CMSIS,
startup and HAL, selection policy, boot counters, MD5 calculation, UART command protocol and page exchange.
A minimal HAL test application can be built on demand with `pwrforge build -p Firmware`; see [firmware build and UART upload](firmware-build.md).
The board assumptions for test are an STM32F103C8T6, 8 MHz HSE (72 MHz system clock),
USART1 on PA9/PA10 at 115200 baud and the PC13 LED.

## Build

From the repository root, using the existing pwrforge Docker setup:

```sh
.venv/bin/pwrforge build -p Debug
.venv/bin/pwrforge build -p MinSizeRel
```

Artifacts are copied by pwrforge to:

```text
build/stm32/<profile>/bin/bootloader.elf
build/stm32/<profile>/bin/bootloader.bin
build/stm32/<profile>/bin/bootloader.hex
```

The map file is under
`build/stm32/<profile>/build/<profile>/bin/bootloader.map`.
Program the BIN at **0x08000000**; HEX carries its addresses.
Building does not flash a device. Avoid mass erase when preserving NV and images.

The 16 KiB bootloader partition requires size optimization, including Debug;
Debug still contains symbols, but stepping through optimized code may skip lines.
The bootloader target is `bootloader`, producing `bootloader.elf`.
`BOOTLOADER_VERSION` defaults to the old `0.0.1` and is a CMake cache setting.

## Generated layout and linker integration

| Region | Start | Size |
| --- | --- | --- |
| Bootloader | 0x08000000 | 0x4000 (16 KiB) |
| NV items | 0x08004000 | 0x1000 (4 KiB) |
| Execution image, FW1 | 0x08005000 | 0x5800 (22 KiB) |
| Stored image, FW2 | 0x0800A800 | 0x5800 (22 KiB) |

Each image begins with a 512-byte header. Application vectors and code are linked
to **0x08005200**, including images downloaded into FW2. FW2 is storage, never an
execution address. Flash ends at 0x08010000; RAM is 20 KiB at 0x20000000.

`src/gen/memory_map/CMakeLists.txt` restores the old `default.ini` values and
generates `generated/memory_map.h`. The existing bootloader linker template is
configured to `src/bootloader/LinkerScript.ld` in the CMake binary tree, then attached:

```cmake
GEN_LINKER_SCRIPT("BOOTLOADER" ${BOOTLOADER_SIZE} ${NV_SIZE} ${FW_SIZE})
stm32_add_linker_script(bootloader PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/LinkerScript.ld")
```

Link to `CMSIS::STM32::F103xB` (the chip type), as this project does.
The device target `CMSIS::STM32::F103C8` also brings a default whole-flash linker
script and should not be combined with this custom script.

The generator also emits `src/gen/memory_map/FirmwareFW1.ld` in the binary tree.
It reserves the header even without an input `.fw_header` section and places
vectors at 0x08005200. The `Firmware` profile uses this template for the HAL
test application, sharing the map, version generator and UART/AT libraries.

`pwrforge.toml` names the artifact `bootloader` and excludes
`src/CMakeLists.txt` from `pwrforge update` so the custom target graph survives
regeneration. The generated top-level CMake file remains unmodified.

## Firmware image preparation

`pwrforge build -p Firmware` now creates a complete, patched
`build/stm32/Firmware/bin/firmware.bin` automatically, along with matching ELF
and HEX files. Use `tools/uart_dfu.py` to upload it over UART; see the
[firmware instructions](firmware-build.md). The manual packer below is also
available for external application payloads.

A linker script controls ELF addresses; the downloadable BIN contains the
512-byte metadata header followed by the application payload. The script itself
is not embedded in the image.

For an application BIN generated with the firmware linker template (already
containing the 512-byte header reservation):

```sh
python3 tools/pack_firmware.py firmware.bin firmware-image.bin \
    --reserved-header --version 0.0.1 --git-sha 0123456789abcdef
```

If the input begins directly with the application vector table, omit
`--reserved-header`. The payload must already be linked at 0x08005200; this tool
does not relocate code. It writes a separate output and does not patch the ELF.

The byte layout matches the original Python ctypes header: little-endian fields,
512-byte data offset, image magic 0xBEEF, type 0, three version bytes, vector
offset 4, reserved zero, eight Git SHA bytes and zero padding.
The checksum is **MD5(little-endian uint16 magic + payload)**.

Download that complete image into FW2 at 0x0800A800, or use the existing DFU
commands with bank 2 and page indices 0..21. The stored reset vector still points
into FW1. Preserve the existing application's NV confirmation behavior so the
boot-count fallback policy works.

## Changes around flash and handoff

HAL erases one 1 KiB page and programs/verifies words. It rejects writes into
the bootloader, beyond flash, with misaligned destinations or excessive size.
The bootloader rejects invalid header bounds, stack pointers, reset vectors and
checksums, revalidates FW1 after swapping, and stays in boot mode on an NV write
failure.

The UART retains its circular buffers with HAL initialization and register-level
IRQ servicing. Handoff drains transmit data, disables USART1 and SysTick, clears
NVIC enables/pending state, sets VTOR, and branches after switching MSP without
using the old C stack.

## UART monitor and regression tests

Connect converter TX to PA10, converter RX to PA9, and converter GND to board
GND, using 3.3 V UART logic. USART1 uses its default PA9/PA10 mapping at 115200,
8 data bits, no parity, one stop bit, and no flow control.

```sh
pwrforge monitor -p /dev/ttyUSB0 -b 115200
```

While the bootloader is polling commands (PC13 blinking), type uppercase `AT`
and press Enter. The installed pwrforge monitor appends CRLF automatically;
the reply is `OK` followed by LF. There is no standalone `t` command.

The temporary `UART_TX_DEBUG` diagnostic in
`src/libs/bootloader_interface/bt_interface_stm32f10x.c` is disabled (`0`).
Keep it disabled during DFU: unsolicited `OK` messages could be mistaken for
command acknowledgements. Startup logging remains disabled in the bootloader
logger implementation; the test firmware sends its own startup banner.

The parser accepts commands up to 1088 bytes, excluding CRLF. It preserves
fragmented and queued commands and discards oversized frames through their
terminating CRLF before resuming. Normal reads and sends do not clear queued
RX or TX bytes. Both send helpers return bytes enqueued, which may be fewer
than requested after one second without progress. They require foreground
execution with UART and SysTick interrupts enabled; returning does not mean
the last byte has finished transmitting.

Run the UART/AT host regressions alongside the bootloader suite:

```sh
python3 tests/uart/run.py
python3 tests/bootloader/run.py
```

The UART suite uses the real driver, circular buffers, parser, dispatcher, and
AT handler with simulated HAL registers and interrupts. It runs with address
and undefined-behavior sanitizers, including framing bounds, queued commands,
ring wraparound, long sends, and stalled-TX recovery. This does not verify
physical pin signals or the installed firmware.

If the board still does not reply, confirm the flashed image matches the new
build, then use a debugger to check the clock, USART1 baud configuration and
remapping, RXNE/USART1 interrupt delivery, command dispatch, and TX output.
Hardware acceptance is one complete `OK` for every repeated `AT` request through
the monitor.

## DFU validation

DFU fixes include a full-page erased buffer fill (the old code cleared only
`sizeof(pointer)` bytes), buffer lifetime, payload bounds, numeric overflow,
hex validation and page-range checks. Command/parameter parsing is bounded by
the received length.

## Validation and remaining safety work

Run the Linux host regression suite (GCC, binutils and CMake; it can use the
project's virtualenv CMake):

```sh
python3 tests/bootloader/run.py
```

It checks the real generated firmware linker layout, Python-to-C image format
compatibility, malformed headers/vectors/checksums, firmware selection, all
22 page exchanges, failure at each of the 44 writes, and DFU overflow/close/reopen
cases. The C tests run with undefined-behavior sanitization.

**This retains the old RAM-scratch swap, which is not power-loss recoverable.**
If power disappears after a source page is erased, its RAM copy is lost.
The tests verify successful exchange and error returns, not recovery across
power interruptions. NV updates also erase and rewrite a single page.

A recoverable exchange needs persistent scratch storage and a journal, resumed
before image selection, with an atomic completion/NV update protocol. A possible
layout uses the three remaining NV pages (0x08004400, 0x08004800, 0x08004C00);
that reservation is not implemented here and must be coordinated with the
application's use of NV. Hardware power-cut tests would then be required.

The legacy MD5 field detects corruption; it does not authenticate the publisher
and is not a cryptographic secure-boot signature. Authentication would require
a separate image-format/key-management change.

No hardware has been flashed or tested as part of this migration. Before using
this build on a device, verify USART DFU, normal boot, FW2-to-FW1 exchange,
application confirmation, failed-boot fallback and recovery mode with a debugger.
Keep power stable during the legacy exchange.

## Repository organization

Keep bootloader and firmware together during the port. Separate CMake targets
can share the memory map, image format and NV structures without linking their
code together. Splitting repositories now would add version-coordination work.
Consider a split only when the application is independently released, with a
versioned shared boot/image contract.
