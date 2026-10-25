# Overview

A secondary bootloader for the **STM32F103C8T6**, built with **STM32Cube HAL/CMSIS and pwrforge**. It supports UART firmware updates, image integrity validation, application boot confirmation, and fallback after repeated unconfirmed boots.

The project uses two firmware slots: **FW1** runs the application, while **FW2** stores an update. Both images are linked for FW1; the bootloader exchanges their contents during activation. A minimal HAL test application and Python UART uploader are included to exercise updates from either application or bootloader mode.

See the [bootloader build, image format and validation notes](doc/bootloader-build.md) and [test firmware and UART upload instructions](doc/firmware-build.md).

*Note*: in all commands `/dev/ttyUSB0` is an example for UART-USB converter such as FT232

To generate bootloader binary : `$ pwrforge build` :
To generate example firmware binary : `$ pwrforge build -p Firmware` :

To flash the generated firmware, and run the fail-safe FW update in both application and bootloader mode run:

`$ python ./tools/uart_dfu.py ./build/stm32/Firmware/bin/firmware.bin --port /dev/ttyUSB0 --activate`

You can build the application with `.venv/bin/pwrforge build -p Firmware` before testing or used pre-builded test version (`./tests/data/firmware1.bin`).
Use the packaged `build/stm32/Firmware/bin/firmware.bin`, which includes the
512-byte header and application vectors at `0x08005200`. The default build
produces `bootloader.bin`, for programming at `0x08000000`; it cannot be uploaded
as an application. Known old application images using the legacy NV format
are also rejected because their startup confirmation can erase a journal page.

Choose `RELAY_NUMBER` near the top of `tools/failsafe_update.py` before testing:
`1` for NRST reset or `2` for power cutoff (the default). Wire the selected
HURTM channel so enabling it pulls NRST to GND or cuts power, and disabling it
releases NRST or restores power. You can also override it with `--relay-number`.
`RELAY_HOLD_MS = 1000.0` keeps each pulse active for 1 second; override it with
`--reset-hold-ms` if needed.
The same 100-run random command below works for either channel:

```sh
sudo -v
python ./tools/failsafe_update.py ./tests/data/firmware1.bin --port /dev/ttyUSB0

or

python ./tools/failsafe_update.py ./tests/data/firmware1.bin --port /dev/ttyUSB0 --log-dir logs --runs 100

or

python tools/failsafe_update.py ./tests/data/firmware1.bin --port /dev/ttyUSB0 --log-dir logs --runs 100 --random-delay --start-delay-ms 1 --max-delay-ms 20 --max-resets 100
```

The test uploads FW completely, selects it, and starts activation with a relay
on/off pulse. It then injects resets, measured from
the return of each relay release command. Scheduling works without bootloader
logs; USB and mechanical relay latency affect the actual reset timing. Use
`--no-sudo` with suitable device permissions, or `--dry-run` to validate settings
without accessing hardware.

After transfer and activation reset, the bootloader validates FW2 before
starting a new swap. This applies to downloads in boot mode and application
mode. If integrity fails, it logs `BL FW2 REJECTED INTEGRITY` and boots validated
FW1 without swapping. If FW1 is invalid too, it reports
`BL NO VALID APPLICATION` and remains in boot mode. The single-bit test always
sends `tests/data/firmware_corrupted_1bit.bin` unchanged and checks the MCU logs
for rejection and return to running FW1. Run it with
`python tools/firmware_integrity_check.py --port /dev/ttyUSB0` after installing
the updated bootloader and a valid application in FW1.

Finally, test runtime failure and automatic rollback with
`python tools/firmware_rollback_check.py --port /dev/ttyUSB0`. It uploads the
static `tests/data/firmware_corrupted_rollback.bin` unchanged. This image has a
valid MD5 and vectors, so the integrity gate accepts it and activation swaps it
into FW1. It faults immediately at startup and resets without confirming boot.
After three failed boot attempts, the bootloader swaps the previous firmware
back into FW1. The test passes only after observing both completed swaps, the
three candidate boots, the original firmware checksum, and its running and
confirmed startup. It also checks that the restored application answers `AT`.
No relay or host-injected resets are used after activation. Start with a working
application that reports `FW confirmed`; see the
[rollback fixture notes](doc/firmware-build.md#automatic-rollback-test) for details.

Use `--log-dir ./logs` to store the default `failsafe-update-<timestamp>.log`
files in a chosen folder, which is created if needed. Each run uses a new
timestamped filename. Optionally add `--log-file custom.log` to choose a name
inside that folder; an absolute `--log-file` path overrides `--log-dir`.
Existing logs and reports are preserved by adding a numeric suffix if needed.

To repeat the complete upload/reset test 100 times:

```sh
sudo -v
python ./tools/failsafe_update.py ./tests/data/firmware1.bin --port /dev/ttyUSB0 --log-dir ./logs --runs 100
```

`--runs` defaults to 1. Every run uploads the image again and starts a fresh reset
schedule. The default delay sweep starts from `--start-delay-ms`. Failures are
recorded and the next run proceeds. Repeated runs have separate numbered logs,
such as `failsafe-update-<timestamp>-run-001.log`; `--log-file custom.log` gives
`custom-run-001.log`, `custom-run-002.log`, etc.

To choose a new random reset delay for every attempt, including the first,
add `--random-delay` and set the delay range. For example, 100 tests with delays
uniformly distributed between 1 and 20 ms:

```sh
sudo -v
python ./tools/failsafe_update.py ./tests/data/firmware1.bin --port /dev/ttyUSB0 \
  --log-dir ./logs --runs 100 --random-delay \
  --start-delay-ms 1 --max-delay-ms 20 --max-resets 100
```

In random mode, `--start-delay-ms` is the minimum and `--max-delay-ms` is the
maximum; `--step-delay-ms` is ignored. `--max-resets` limits injected resets per
test (default 100 in random mode), then allows a final recovery observation
window. It can also cap the default increasing sweep. Both `--delay-from`
references work with random delays. Application recovery, errors, and the UART
silence watchdog can finish a test earlier. If the application runs and confirms
before any injected reset, the run passes and logs that no resets were injected.

Optionally pass `--random-seed 42` to repeat the requested delay sequences.
Otherwise, a seed is generated automatically. The report records the base seed;
each run uses `base seed + run number - 1` and records its seed in its own log.
To replay one run's requested delays, use that log's seed with `--runs 1` and
the same delay settings. Actual timing still depends on USB, scheduling, and
relay latency. Each reset logs its requested delay, command dispatch time, and
command return time; the final PASS line reports the last reset delay used.

A `<log-name>-summary.txt` report is saved alongside the logs and updated after
each run. It lists passed, failed, aborted, and unstarted run counts, plus each
run's result and log path. A completed test passes only when the application is
observed running and confirming startup; all other completed tests fail,
including a reset limit reached without application recovery. These counts
describe complete tests, not individual reset pulses.
The report is also printed when the loop finishes. Ctrl+C or SIGTERM stops the
loop and saves the partial report. The exit code is 0 if every run passes,
1 if any run fails, or 130 if interrupted.

After `--boot-timeout` seconds without UART lines across pulses (default 5 s),
the test stops injecting resets and gives the current boot another full timeout
to finish. It reports `FAIL` if no running application confirms startup. Output received during a
relay command is logged before clearing the previous boot's buffer, so errors
and firmware completion during a slow relay command are retained.

Repeated `BL START` banners stop further cuts and start a bounded recovery
observation. They do not fail the run by themselves: the application can still
recover and pass within `--boot-timeout`. Further boot or application banners
do not extend that window. A relay command returning does not timestamp the actual contact or
power transition. Requested 1..20 ms delays therefore do not establish physical
1..20 ms power-cut timings.

`--delay-from swap-start` instead starts each delay at `BL SWAP START` and requires
a bootloader that emits that marker. A `PASS` requires a fresh `FW ... running`
followed by `FW confirmed` or `FW confirmation disabled` after the latest relay
reset. Either the uploaded application or the previous application recovered
by fallback counts as success. `BL FW VERIFIED` logs image identity and stops
further resets while waiting for the application; checksum verification alone
cannot pass a test. Application startup also stops further resets and waits for
confirmation. Bootloader markers and proof of a reset during a swap are
diagnostics, not pass requirements. Boot errors, failed confirmation, UART or
relay errors, and recovery timeouts fail the run. Output buffered before a relay
reset cannot establish that the application recovered after that reset.
Host regression checks: `python tests/it/test_failsafe_update.py`.

To see the logs over uart run: `$ pwrforge monitor -p /dev/ttyUSB0` 

## Getting started with pwrforge project

### Development with devcontainer in visual studio code

#### Devcontainer

1. Install Visual Studio Code Dev Containers.
2. Install docker engine [https://docs.docker.com/get-docker/](https://docs.docker.com/get-docker/)
3. Development environment is defined as docker image defined inside .devcontainer directory so no additional installations are required (It is also used by CI so you use is what CI use).

#### Linux (debian/ubuntu) issues: docker compose is not installed by default

docker-compose is not installed by default with docker engine so below might be required

```bash
#!/bin/bash

sudo apt install docker-compose
```

#### Windows 11 issues: Usb is not available in container

If you use windows and would like to have device access. You would be forced to forward them. It can be done via usbip tool (usb over ip). So

1. Put your Docker Desktop into WSL2 mode (Best during installation, in version v4.18 it is default option).
2. Change default Docker desktop WSL image to UbuntuDistribution (default one is not debian based and will NOT work with usbip instructions). Best  way is to switch it from docker desktop GUI
    1. Go to DockerDesktopTrayIcon (right click) -> Setting -> Resources -> WSL Integration
    2. Find option described as *Enable integration with additional distros* And mark ubuntu 22.04 option
3. Change default WSL image to Ubuntu 22.04
    ```powershell
    wsl -l -v                      # check the names
    wsl --set-default ubuntu-24.04 # set default distribution
    ```
4. Install usbip on windows and inside WSL ubuntu-24.04
5. Attach device
    ```
    usbipd list                   # print all usb devices
    usbipd wsl attach --busid 4-2	# attach device 4-2 to docker run env
    ```

#### Sources

1. [https://learn.microsoft.com/en-us/windows/wsl/connect-usb](https://learn.microsoft.com/en-us/windows/wsl/connect-usb)
2. [https://github.com/dorssel/usbipd-win](https://github.com/dorssel/usbipd-win)
3. [https://devblogs.microsoft.com/commandline/connecting-usb-devices-to-wsl/](https://devblogs.microsoft.com/commandline/connecting-usb-devices-to-wsl/)

### Work with project (compilation)

#### pwrforge flow (local development)

```bash
#!/bin/bash
pwrforge clean;                 # Clean build directory.
pwrforge update;                # GENERATE files using toml file as instruction (pwrforge is code generator). IT IS REQUIRED AFTER CHANGES IN *.toml FILE
pwrforge build --profile Debug; # Compile project with debug profile
pwrforge test -v;               # Run tests with verbose output
```

#### pwrforge full flow (tools and checks)

```bash
#!/bin/bash
pwrforge clean;                 # Clean build directory.
pwrforge update;                # GENERATE files using toml file as instruction (pwrforge is code generator). IT IS REQUIRED AFTER CHANGES IN pwrforge.toml FILE
pwrforge test -v;               # Run tests with verbose output
pwrforge build --profile Debug; # Compile project with debug profile
pwrforge check --clang-format;  # Check sources with clang format (autoformatter)
pwrforge check --cppcheck;      # Check sources with cppcheck (static analysis)
pwrforge check --clang-tidy;    # Check sources with clang format (naming convention and light static analysis)
pwrforge check --todo;          # Check for todo comments
pwrforge check --cyclomatic;    # Calculate cyclomatic complexity
pwrforge check --pragma;        # Check for existence of pragma directive in headers
pwrforge check --copyright;     # Check for existence of copyright in files
pwrforge fix --clang-format;    # Run automate clang formatting for files with issues
pwrforge fix --copyright;       # Add copyright notice at start of source files (if missing)
pwrforge fix --pragma;          # Add pragma directive in headers if missing
pwrforge doc;                   # Generate documentation
pwrforge publish;               # Publish conan package to artifacts repository
pwrforge flash;                 # Flash microcontroller board
```

#### Other pwrforge commands (not mandatory for all projects)

```bash
#!/bin/bash
pwrforge gen #Generate certificate and other artifacts for chosen targets
```

### Debug

#### debug in the console

`pwrforge debug`

If OpenOCD fails to start, check `.devcontainer/openocd-debug.log`.

To run only the OpenOCD server, use `pwrforge debug --openocd`.
To run only GDB and attach to an already running server, use `pwrforge debug --gdb`.

#### debug in vsc

- run debug in vscode

## Potential issues

### stlink issue

.devcontainer/openocd-script.cfg add if needed:

`set CPUTAPID 0x2ba01477`

This ST-LINK/STM32F1 combination reports the SW-DP v2 ID.
OpenOCD now starts successfully, detects the STM32F1 Cortex-M3, and listens on GDB port 3333 without the unexpected IDCODE error.

```sh
Warn : UNEXPECTED idcode: 0x2ba01477
Error: expected 1 of 1: 0x1ba01477
```

#### usb relay

```sh
pip install hid-usb-relay
sudo nano /etc/udev/rules.d/99-usbrelay.rules
SUBSYSTEM=="hidraw", ATTRS{idVendor}=="16c0", ATTRS{idProduct}=="05df", MODE="0660", GROUP="plugdev", TAG+="uaccess"
SUBSYSTEM=="usb", ATTR{idVendor}=="16c0", ATTR{idProduct}=="05df", MODE="0660", GROUP="plugdev", TAG+="uaccess"

sudo udevadm control --reload-rules
sudo udevadm trigger
```

#### UART

Set communication priviledges for UART-USB controller

`sudo usermod -aG dialout $USER`
