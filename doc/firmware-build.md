# Test firmware for the secondary bootloader

Build the application on demand from the repository root:

```sh
.venv/bin/pwrforge build -p Firmware
```

The result is `build/stm32/Firmware/bin/firmware.bin`, ready for UART DFU.
The same directory contains `firmware.elf`, `firmware.hex` and `firmware.map`.
The BIN, ELF and HEX all contain the same populated header and payload.
The packaged image must fit its 22 KiB slot.
Normal `Debug` and `MinSizeRel` profiles continue to build the bootloader.

`Firmware` uses size optimization with debug symbols. Its settings are in
`config/conan/profiles/stm32_Firmware`, which is excluded from `pwrforge update`.
Both `pwrforge.toml` and `pwrforge.lock` register this profile: pwrforge 0.1 reads
the lock file when building. No `pwrforge update` is needed before this build.

## Application behavior

The application uses STM32Cube HAL with the same 8 MHz HSE / 72 MHz clock,
USART1 PA9/PA10 at 115200 8N1 and PC13 LED as the bootloader. It shares the
clock/vector initialization, UART driver, AT parser, command handlers, flash
driver, NV structures and memory-map/version generators.

Startup sets VTOR to `0x08005200` before enabling HAL's SysTick, then prints
`FW 0.0.2 running`. After one second it confirms the application in NV by setting
`app_valid=1` and `boot_count=0`, prints `FW confirmed`, and starts processing AT
commands. A failed NV write prints `FW confirmation failed`. Confirmation is
attempted once per startup, so later `AT+RAA` commands remain effective. PC13
flashes twice per two-second cycle: 100 ms on, 100 ms off, 100 ms on, then
1.7 seconds off. The pattern does not block UART command handling.

This small foreground loop replaces the old application's FreeRTOS/BSP path.
The legacy health-manager, calibration and NV-driver files remain excluded from
the target. There is no application watchdog or automatic reset.

## Image format and single-bank layout

The MCU has one physical flash bank and two software image slots. Every firmware
is linked for execution from FW1; FW2 holds an update until the bootloader swaps
it into FW1.

| Item | Address / size |
| --- | --- |
| FW1 header / execution slot | `0x08005000` / `0x5800` bytes |
| FW1 vector table | `0x08005200` |
| FW2 storage slot | `0x0800A800` / `0x5800` bytes |
| Metadata header | 512 bytes |
| Maximum payload | 22,016 bytes |
| DFU page / bank pages | 1,024 bytes / 22 pages |

Upload the **complete `firmware.bin`**, including its header, into FW2. Do not
link a separate FW2 executable or relocate the reset vector to FW2. The ELF/HEX
addresses describe FW1 and are useful for direct FW1 flashing/debugging; the UART
uploader uses the BIN. All nonempty ELF load segments are confined to the image.

`tools/firmware_header.py` reuses the header implementation from
`cdf_stm32/common_dev/scripts/patch_image/firmware_header.py`, with explicit
little-endian byte order and input length checks. `tools/pack_firmware.py` uses
the original magic-seeded MD5 calculation and eight-byte Git SHA prefix:

```text
MD5(little-endian uint16 0xBEEF + application payload)
```

The metadata contains payload length, offset 512, MD5, magic, image type 0,
three version bytes, vector offset 4, reserved zero and Git SHA. Header padding
is zero. Git SHA is captured at CMake configuration; source archives without Git
use eight zero bytes. It identifies the base commit, not uncommitted changes.

CMake supplies the metadata instead of the old INI parser. GNU objcopy patches
the ELF header and emits HEX, so this build does not require LIEF or GitPython.
Intermediate unpatched files stay under the CMake firmware directory; only
packaged images are exported to `bin/`. Python rejects images with invalid
vectors or excessive size, and the linker enforces the flash/RAM regions.

## UART upload

Connect 3.3 V converter TX to PA10, RX to PA9, and GND to board GND. Close any
serial monitor before opening the uploader. The installed bootloader must
support `AT`, `AT+RAA`, `AT+RESET` and the existing DFU commands.

```sh
.venv/bin/python tools/uart_dfu.py build/stm32/Firmware/bin/firmware.bin \
    --port /dev/ttyUSB0 --activate
```

The host Python environment needs pyserial (already a pwrforge dependency).
Use the secondary bootloader's normal UART pins with BOOT0 low. This protocol
does not use STM32's factory ROM loader. The installed `pwrforge flash` command
uses ST-Link and the bootloader's base address; use `uart_dfu.py` for this update.

The uploader validates the image locally, checks `AT`, selects persistent boot
mode with `AT+RAA=11`, then resets into the bootloader. It sends:

```text
AT+DFUOP=2
AT+DFUWR=512,<512 uppercase hex characters representing 256 bytes>
...four data chunks per page...
AT+DFUWR=<page number>
...remaining pages, with page 0 committed last...
AT+DFUCL
AT+RAA=2
AT+RESET
```

Each command ends in CRLF and waits for `OK` followed by LF. RESET has no reply.
The first DFUWR argument for data is the number of **hex characters**, not bytes.
The last partial page is padded with `0xFF`; those padding bytes are outside the
header's payload length and checksum. Page indices are relative to FW2.
The uploader stops on an error or timeout without requesting activation.
`AT+DFUCL` finishes the transfer. After `AT+RAA=2` and reset, the bootloader checks
the image in flash before starting a new swap, regardless of whether it was
downloaded in boot mode or application mode. Invalid FW2 produces
`BL FW2 INVALID` and `BL FW2 REJECTED INTEGRITY`. If FW1 passes validation, the
bootloader persists its selection, reports `BL RETURN FW1`, and boots FW1
without swapping. If both images are invalid, it reports
`BL NO VALID APPLICATION` and stays in boot mode.

Omit `--activate` to leave the board in boot mode after uploading. Later send
`AT+RAA=2` and `AT+RESET` to swap and boot. Restart an interrupted upload from the
beginning. Use `--dry-run` to validate an image and print its DFU data commands
without opening a port.

After activation, check the `FW <version> running` and `FW confirmed` messages
and the LED. An upload acknowledgement means the page write succeeded; the
bootloader checks the complete image before swapping on reset. The uploader does not read flash
back or claim that the application has booted.

## Different versions and failed-boot tests

Edit `FIRMWARE_VERSION` in `config/conan/profiles/stm32_Firmware`, for example
from `"0.0.2"` to `"0.0.3"`, then rerun the same pwrforge command. Each component
must be 0..255. Save the first BIN under another name before rebuilding if you
want two test images. The header and startup banner use the same version.

Set `FIRMWARE_CONFIRM_BOOT` to `False` in that profile to produce an application
that prints `FW confirmation disabled` and never confirms itself. Activate it
and manually reset it repeatedly to exercise the existing failed-boot counter
and fallback policy. Reset count/slot selection remain the bootloader's policy.
Restore `True` for normal test images. The CMake options `SBOOT_BUILD_BOOTLOADER`
and `SBOOT_BUILD_FIRMWARE` can also both be enabled in a configured build to emit
separate images; generating a combined flash image is future work.

## Automatic rollback test

Run this after the integrity-rejection and failsafe-update tests, with a working
application that confirms startup installed in FW1:

```sh
.venv/bin/python tools/firmware_rollback_check.py --port /dev/ttyUSB0
```

The test first resets the working application, records its verified checksum,
and requires `FW ... running` followed by `FW confirmed`. It then uploads the
checked-in `tests/data/firmware_corrupted_rollback.bin` unchanged, selects FW2,
and requests activation once. It never recalculates the uploaded file's MD5 or
injects resets after activation.

This fixture is a deliberately broken Cortex-M3 application with a valid header,
magic-seeded MD5, stack pointer and Thumb reset vector. Its reset handler executes
`UDF #0`, causing a HardFault. The fault handler requests a system reset through
SCB AIRCR (`VECTKEY | SYSRESETREQ`). It does not write flash/NV, initialize UART,
print a running banner, or confirm boot. Explicit fault resets make the test
autonomous even though the normal application has no active watchdog. A fixture
that merely hangs would need an independent reset source to advance boot trials.

The required sequence is:

1. `BL FW2 VALID`, then a completed activation swap (`BL SWAP START` / `BL SWAP DONE`).
2. Three distinct boots verifying the faulty image, with no application confirmation.
3. A second completed swap after the failed-boot limit is reached.
4. The original application's `BL FW VERIFIED` checksum, `FW ... running`,
   `FW confirmed`, and an `OK` reply to `AT`.

Integrity rejection, a hanging candidate, extra failed boots, an incomplete
swap, a different restored image, or missing application confirmation fails the
test. `BL SWAP ROLLBACK` describes a separate post-copy integrity recovery path;
that marker alone cannot establish failed-boot rollback. The default
`--boot-timeout 30` bounds the whole activation/retry/rollback observation;
`--timeout 5` bounds each AT exchange. Exit codes are 0 for PASS, 1 for FAIL,
and 130 for interruption. Use `--dry-run` to validate the fixed fixture without
accessing hardware.

The fixture source is `tests/data/firmware_corrupted_rollback.S`. To reproduce
the 1,024-byte packed binary using Clang/LLVM 18 and the existing packer:

```sh
clang --target=arm-none-eabi -mcpu=cortex-m3 -mthumb \
  -c tests/data/firmware_corrupted_rollback.S -o /tmp/sboot-rollback.o
llvm-objcopy-18 -O binary --only-section=.text \
  /tmp/sboot-rollback.o /tmp/sboot-rollback-payload.bin
python tools/pack_firmware.py /tmp/sboot-rollback-payload.bin \
  tests/data/firmware_corrupted_rollback.bin --version 0.0.255
```

Host checks, including the real C image-validation, swap, and failed-boot policy:

```sh
python tests/it/test_firmware_rollback_check.py
python tests/bootloader/run.py tests/data/firmware_corrupted_rollback.bin
```

The C checks emulate unconfirmed reboots and verify both complete bank contents;
the UART hardware test verifies the fixture's actual fault/reset behavior.

## Validation and power interruption limits

```sh
python3 tests/firmware/run.py build/stm32/Firmware/bin/firmware.bin
python3 tests/bootloader/run.py build/stm32/Firmware/bin/firmware.bin
python3 tests/uart/run.py
```

These host checks verify matching BIN/ELF/HEX contents and flash bounds, uploader
control flow, original checksum test vectors, and transfer of the built ARM
image through the real C DFU handlers into simulated flash. The bootloader's
real image checker accepts it in both slots. The bootloader suite also checks
selection, swaps, write failures and malformed requests with UBSan. Passing the
real image exposed a signed-shift issue in MD5 decoding; the byte shifts now
use unsigned arithmetic. No device is flashed by these tests or by the build.

The checked-in bootloader still exchanges pages using RAM scratch and updates
NV by erasing/replacing one flash page. Those operations are **not power-loss
recoverable**. This application supplies test images and observable startup;
it does not implement a persistent swap journal. Keep power stable during
activation until the bootloader's recovery implementation is ready. See
[bootloader build notes](bootloader-build.md#validation-and-remaining-safety-work).
