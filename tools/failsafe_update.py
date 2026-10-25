#!/usr/bin/env python3
"""Upload FW2 and interrupt activation with a USB relay on NRST or power, optionally repeating."""

import argparse
from collections import deque
from datetime import datetime
import math
from pathlib import Path
import random
import signal
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
from uart_dfu import check_nv_compatibility, exchange, upload, validate_image

SWAP_START = b"BL SWAP START"
VERIFIED = b"BL FW VERIFIED "
RESULT_NAMES = {0: "PASS", 1: "FAIL", 130: "ABORTED"}
RELAY_NUMBER = 2  # Choose before testing: 1 = NRST reset, 2 = power cutoff.
RELAY_HOLD_MS = 1000.0  # Keep reset asserted or power cut for 1 second.


class Relay:
    """Same usbrelay interface as relay_test.py: enabled asserts reset or cuts power."""

    def __init__(self, board, number, use_sudo=True, command="usbrelay", timeout=5.0):
        self.channel = f"{board}_{number}"
        self.prefix = (["sudo", "-n"] if use_sudo else []) + [command]
        self.timeout = timeout

    def set(self, enabled):
        # Never prompt for a password in the timed loop. Run sudo -v beforehand.
        subprocess.run(self.prefix + [f"{self.channel}={int(enabled)}"],
                       check=True, timeout=self.timeout, stdout=subprocess.DEVNULL)


class BootMonitor:
    """Keep fragmented UART lines without blocking past a reset deadline."""

    def __init__(self, port, report):
        self.port = port
        self.report = report
        self.buffer = bytearray()
        self.lines = deque()

    def clear(self):
        # Called while NRST is held low, never after release: retain new boot logs.
        self.port.reset_input_buffer()
        self.buffer.clear()
        self.lines.clear()

    def feed(self, data, received):
        self.buffer.extend(data)
        while b"\n" in self.buffer:
            line, _, rest = self.buffer.partition(b"\n")
            self.buffer = bytearray(rest)
            # Reset/UART handoff can leave a NUL before the first banner.
            line = bytes(line).strip(b"\x00 \t\r\n")
            if line:
                self.lines.append((received, line))
        if len(self.buffer) > 4096:
            raise RuntimeError("unterminated boot log exceeds 4096 bytes; check UART baud/wiring")

    def drain(self):
        """Collect the previous boot's output before clearing it under NRST."""
        self.port.timeout = 0
        while self.port.in_waiting:
            data = self.port.read(self.port.in_waiting)
            if not data:
                break
            self.feed(data, time.monotonic())
        events = list(self.lines)
        self.lines.clear()
        return events

    def read_until(self, deadline):
        while True:
            if self.lines:
                received, line = self.lines[0]
                if received > deadline:
                    return None
                self.lines.popleft()
                self.report(f"RX {line.decode('ascii', errors='backslashreplace')}", received)
                return received, line
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                return None
            # readline() with a long timeout can swallow the entire 1 ms window.
            self.port.timeout = min(remaining, 0.01)
            data = self.port.read(self.port.in_waiting or 1)
            received = time.monotonic()
            self.feed(data, received)


def reset_pulse(relay, monitor, hold_ms, report, started=None, requested_ms=None,
                reference="reset release"):
    dispatched = time.monotonic()
    relay.set(True)
    asserted = time.monotonic()
    if started is None:
        report("ACTIVATION RESET asserted via relay")
    else:
        report(f"RESET requested={requested_ms:g} ms; command dispatched at "
               f"{(dispatched - started) * 1000:.3f} ms after {reference}; "
               f"usbrelay returned at {(asserted - started) * 1000:.3f} ms")
    time.sleep(hold_ms / 1000)
    # usbrelay blocks while the target may still run. Preserve its output:
    # a verification received here belongs to the boot BEFORE this reset.
    pending = monitor.drain()
    for received, line in pending:
        report(f"RX buffered during relay pulse: {line.decode('ascii', errors='backslashreplace')}", received)
    if monitor.buffer:
        report(f"RX partial before reset: {bytes(monitor.buffer)!r}")
    monitor.clear()
    relay.set(False)
    released = time.monotonic()
    report("RESET released", released)
    return released, pending


def check_boot_error(line):
    if (line == b"BL BOOT MODE" or line.startswith(b"BL ERROR ") or
            line == b"FW confirmation failed"):
        raise RuntimeError(f"activation/recovery failed: {line.decode('ascii')}")


def sweep(monitor, relay, digest, args, report, released, rng=None):
    delay_ms = args.start_delay_ms
    last_reset_delay = None
    pulses = 0
    limit_reached = False
    last_output = released
    observation = None
    from_marker = args.delay_from == "swap-start"
    reference = "swap-start" if from_marker else "reset release"
    while True:
        if rng is not None and not limit_reached and not observation:
            delay_ms = rng.uniform(args.start_delay_ms, args.max_delay_ms)
        started = None if from_marker or limit_reached or observation else released
        deadline = released + args.boot_timeout if started is None else started + delay_ms / 1000
        if started is not None:
            report(f"Attempt {pulses + 1}: reset scheduled {delay_ms:g} ms after {reference}", started)
        boot_seen = False
        swap_seen = False
        firmware_started = False
        while True:
            # Silence is measured across pulses. Restarting this watchdog at
            # each release would allow thousands of resets with no boot output.
            watch_silence = not (limit_reached or observation or firmware_started) and started is not None
            wait_until = min(deadline, last_output + args.boot_timeout) if watch_silence else deadline
            event = monitor.read_until(wait_until)
            if event is None:
                if watch_silence and time.monotonic() >= last_output + args.boot_timeout:
                    observation = "UART silence"
                    deadline = time.monotonic() + args.boot_timeout
                    report(f"No UART boot output for {args.boot_timeout:g} s across {pulses} reset pulses; "
                           f"reset injection stopped. Listening for {args.boot_timeout:g} s without resets.")
                    continue
                if firmware_started:
                    raise TimeoutError("firmware started but did not confirm before boot timeout")
                if observation:
                    raise TimeoutError(f"no running firmware after {observation} and "
                                       f"{args.boot_timeout:g} s without resets; relay is released. "
                                       "Check UART/relay wiring, power and device recovery; increasing the reset "
                                       "delay cannot establish recovery from this run")
                if limit_reached:
                    raise TimeoutError(f"no running firmware after {pulses} reset pulses and "
                                       f"{args.boot_timeout:g} s without resets; "
                                       f"last reset delay {last_reset_delay:g} ms")
                if started is None:
                    raise TimeoutError("no swap-start marker before boot timeout; use "
                                       "--delay-from reset-release for a bootloader without markers")
                break

            received, line = event
            last_output = received
            check_boot_error(line)
            if line.startswith(VERIFIED):
                identity = ("uploaded firmware" if line == VERIFIED + digest.encode("ascii")
                            else "another firmware image (fallback allowed)")
                report(f"Bootloader verified {identity}; waiting for the application to run", received)
                if not observation and not limit_reached and not firmware_started:
                    observation = "bootloader checksum verification"
                    deadline = received + args.boot_timeout
                    started = None
                continue
            if line == SWAP_START:
                if swap_seen or firmware_started:
                    if not observation and not limit_reached:
                        observation = "repeated swap-start"
                        deadline = received + args.boot_timeout
                        started = None
                        report("Repeated swap-start; reset injection stopped. Observing recovery.", received)
                    firmware_started = False
                swap_seen = True
                if from_marker and not limit_reached and not observation:
                    started = received
                    deadline = started + delay_ms / 1000
                    report(f"Attempt {pulses + 1}: reset scheduled {delay_ms:g} ms after {reference}", received)
            elif line == b"BL START":
                if boot_seen or swap_seen or firmware_started:
                    # usbrelay may return after the MCU has already booted.
                    # Consecutive banners can describe events during power
                    # restoration, before host timing starts. Preserve the
                    # anomaly, stop cuts and observe recovery with a fixed limit.
                    if not observation and not limit_reached:
                        observation = "repeated boot banners after relay release"
                        deadline = received + args.boot_timeout
                        started = None
                        report(f"Additional BL START observed {(received - released) * 1000:.3f} ms "
                               "after relay command returned; reset injection stopped. "
                               f"Observing recovery for {args.boot_timeout:g} s; "
                               "physical power/reset timing is uncertain.")
                    swap_seen = firmware_started = False
                boot_seen = True
            elif line.startswith(b"FW ") and line.endswith(b" running"):
                if firmware_started:
                    report("Firmware restarted; still waiting for confirmation within the recovery window", received)
                # Activation has ended. Allow confirmation instead of resetting
                # an already running application indefinitely when BL logs are absent.
                firmware_started = True
                if not observation and not limit_reached:
                    observation = "firmware startup"
                    deadline = received + args.boot_timeout
                started = None
                report("Firmware started; waiting for confirmation without further resets", received)
            elif firmware_started and line in (b"FW confirmed", b"FW confirmation disabled"):
                timing = (f"; last reset delay {last_reset_delay:g} ms"
                          if last_reset_delay is not None else "; no injected resets")
                report(f"PASS: firmware application running ({line.decode('ascii')}) "
                       f"after {pulses} reset pulses{timing}")
                return 0

        released, pending = reset_pulse(relay, monitor, args.reset_hold_ms, report,
                                        started, delay_ms, reference)
        last_reset_delay = delay_ms
        pulses += 1
        completed_before_reset = False
        for received, line in pending:
            last_output = received
            check_boot_error(line)
            if line.startswith(VERIFIED) or (line.startswith(b"FW ") and line.endswith(b" running")):
                completed_before_reset = True
        if completed_before_reset:
            observation = "firmware completion during the relay pulse"
            report("Firmware completed before reset release; this pulse cannot prove swap interruption. "
                   "Reset injection stopped; waiting for a fresh boot result.")
        if ((rng is None and delay_ms >= args.max_delay_ms) or
                (args.max_resets is not None and pulses >= args.max_resets)):
            # Give recovery a complete observation window after the last pulse.
            limit_reached = True
            report("Final reset complete; waiting for the firmware application to run and confirm")
        elif rng is None:
            delay_ms = min(delay_ms + args.step_delay_ms, args.max_delay_ms)


def run_test(port, relay, image, args, report, random_seed=None):
    validate_image(image)
    check_nv_compatibility(image)
    try:
        rng = None
        if args.random_delay:
            seed = args.random_seed if random_seed is None else random_seed
            rng = random.Random(seed)
            report(f"Random seed: {seed}")
        relay.set(False)
        # Let the test application's one-time NV confirmation finish before RAA.
        time.sleep(1.5)
        port.reset_input_buffer()
        report(f"Preparing {len(image)}-byte FW2 upload; reset injection is inactive")
        upload(port, image, activate=False, timeout=args.timeout, report=report)
        digest = image[8:24].hex()
        report(f"Upload committed and closed; expected image checksum {digest}")
        exchange(port, "AT+RAA=2", args.timeout)
        monitor = BootMonitor(port, report)
        # Activate through NRST so the first timed window has a physical reset
        # reference even on installed bootloaders that emit no BL markers.
        released, _ = reset_pulse(relay, monitor, args.reset_hold_ms, report)
        return sweep(monitor, relay, digest, args, report, released, rng)
    finally:
        # Also release NRST after upload errors, serial/relay errors and Ctrl+C.
        relay.set(False)


def parse_args(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("image", type=Path, help="packed firmware.bin including the 512-byte header")
    parser.add_argument("--port", help="UART port, e.g. /dev/ttyUSB0")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--relay-board", default="HURTM")
    parser.add_argument("--relay-number", type=int, default=RELAY_NUMBER)
    parser.add_argument("--relay-command", default="usbrelay", help="usbrelay executable/path")
    parser.add_argument("--no-sudo", action="store_true", help="run usbrelay directly (udev permissions required)")
    parser.add_argument("--relay-timeout", type=float, default=5.0, help="usbrelay command timeout in seconds")
    parser.add_argument("--delay-from", choices=("reset-release", "swap-start"), default="reset-release",
                        help="start each delay when the relay release command returns (default), "
                             "or on BL SWAP START (requires bootloader markers)")
    parser.add_argument("--start-delay-ms", type=float, default=1.0,
                        help="initial sweep delay or minimum random delay in ms (default: 1)")
    parser.add_argument("--step-delay-ms", "--step-delay", type=float, default=10.0,
                        help="sweep increment in ms; ignored with --random-delay (default: 10)")
    parser.add_argument("--max-delay-ms", type=float, default=60000.0,
                        help="maximum delay per attempt, not total run time (default: 60000)")
    parser.add_argument("--random-delay", action="store_true",
                        help="choose each reset delay uniformly between --start-delay-ms and --max-delay-ms")
    parser.add_argument("--random-seed", type=int,
                        help="replay requested random delays; each run uses seed + run number - 1 "
                             "(default: generated and logged)")
    parser.add_argument("--max-resets", type=int,
                        help="maximum injected resets per run (default: 100 with --random-delay, "
                             "otherwise until --max-delay-ms)")
    parser.add_argument("--reset-hold-ms", type=float, default=RELAY_HOLD_MS,
                        help="hold reset asserted or power cut in ms (default: %(default)g)")
    parser.add_argument("--boot-timeout", type=float, default=5.0,
                        help="maximum UART silence across pulses before stopping injection, "
                             "then seconds to observe recovery without resets; also the "
                             "swap-marker/application-confirmation timeout (default: 5)")
    parser.add_argument("--timeout", type=float, default=5.0, help="AT command timeout in seconds")
    parser.add_argument("--runs", type=int, default=1,
                        help="number of complete upload/reset tests (default: 1); continue after failures")
    parser.add_argument("--log-dir", type=Path, default=Path("."),
                        help="log folder, created if needed (default: current directory)")
    parser.add_argument("--log-file", type=Path,
                        help="filename relative to --log-dir, or an absolute path "
                             "(default: failsafe-update-<timestamp>.log); repeated runs add -run-NNN")
    parser.add_argument("--dry-run", action="store_true", help="validate image/settings without UART or relay access")
    args = parser.parse_args(argv)
    if args.runs <= 0:
        parser.error("--runs must be positive")
    if args.max_resets is not None and args.max_resets <= 0:
        parser.error("--max-resets must be positive")
    if args.random_seed is not None and not args.random_delay:
        parser.error("--random-seed requires --random-delay")
    for name in ("baud", "relay_number", "relay_timeout", "start_delay_ms", "step_delay_ms",
                 "max_delay_ms", "reset_hold_ms", "boot_timeout", "timeout"):
        value = getattr(args, name)
        if not math.isfinite(value) or value <= 0:
            parser.error(f"--{name.replace('_', '-')} must be finite and positive")
    if args.start_delay_ms > args.max_delay_ms:
        parser.error("--start-delay-ms must not exceed --max-delay-ms")
    if not args.relay_board or not args.relay_command:
        parser.error("relay board and command must not be empty")
    if not args.port and not args.dry_run:
        parser.error("--port is required unless --dry-run is used")
    if args.random_delay:
        if args.max_resets is None:
            args.max_resets = 100
        if args.random_seed is None:
            args.random_seed = random.SystemRandom().getrandbits(64)
    return args


def open_unique(path):
    """Create an output file without overwriting an earlier run or report."""
    path.parent.mkdir(parents=True, exist_ok=True)
    candidate = path
    suffix = 0
    while True:
        try:
            return candidate.open("x", encoding="utf-8")
        except FileExistsError:
            suffix += 1
            candidate = path.with_name(f"{path.stem}-{suffix}{path.suffix}")


def run_once(serial_factory, image, args, settings, number, log):
    origin = time.monotonic()

    def report(message, received=None):
        elapsed = (time.monotonic() if received is None else received) - origin
        line = f"[{elapsed:12.6f}s] {message}"
        print(line, flush=True)
        log.write(line + "\n")
        log.flush()

    try:
        report(f"Run {number}/{args.runs}: {settings}")
        report(f"Log: {Path(log.name).resolve()}")
        report("Timing is host-observed; USB transport, scheduling and mechanical relay latency apply")
        relay = Relay(args.relay_board, args.relay_number, not args.no_sudo,
                      args.relay_command, args.relay_timeout)
        seed = args.random_seed + number - 1 if args.random_delay else None
        with serial_factory(args.port, args.baud, timeout=0.01, write_timeout=args.timeout) as port:
            return run_test(port, relay, image, args, report, random_seed=seed)
    except KeyboardInterrupt:
        report("ABORTED: interrupted by user")
        return 130
    except subprocess.CalledProcessError as error:
        if error.returncode in (-signal.SIGINT, -signal.SIGTERM):
            report("ABORTED: relay command interrupted")
            return 130
        report(f"FAIL: {error}")
        return 1
    except (OSError, ValueError, RuntimeError, subprocess.SubprocessError) as error:
        report(f"FAIL: {error}")
        return 1


def write_summary(summary, args, results):
    counts = {code: sum(result == code for result, _ in results) for code in RESULT_NAMES}
    lines = ["Failsafe update report",
             f"Image: {args.image.resolve()}",
             f"Requested runs: {args.runs}",
             f"Completed runs: {len(results) - counts[130]}",
             f"Passed: {counts[0]}",
             f"Failed: {counts[1]}",
             f"Aborted: {counts[130]}",
             f"Not started: {args.runs - len(results)}", "",
             "Run\tResult\tLog"]
    if args.random_delay:
        lines[2:2] = [f"Random delay range: {args.start_delay_ms:g}..{args.max_delay_ms:g} ms "
                      f"from {args.delay_from}",
                      f"Maximum resets per run: {args.max_resets}",
                      f"Random base seed: {args.random_seed} (run seed = base + run number - 1)"]
    lines.extend(f"{number}\t{RESULT_NAMES[code]}\t{path}"
                 for number, (code, path) in enumerate(results, 1))
    text = "\n".join(lines) + "\n"
    summary.seek(0)
    summary.write(text)
    summary.truncate()
    summary.flush()
    return text


def run_repeated(serial_factory, image, args, settings):
    path = args.log_dir / (args.log_file or Path(f"failsafe-update-{datetime.now():%Y%m%d-%H%M%S}.log"))
    results = []
    aborted = False

    def interrupted(signum, frame):
        raise KeyboardInterrupt

    summary_path = path.with_name(f"{path.stem}-summary.txt")
    with open_unique(summary_path) as summary:
        print(f"Report: {Path(summary.name).resolve()}", flush=True)
        previous = signal.signal(signal.SIGTERM, interrupted)
        try:
            write_summary(summary, args, results)
            for number in range(1, args.runs + 1):
                log_path = (path if args.runs == 1 else
                            path.with_name(f"{path.stem}-run-{number:03d}{path.suffix}"))
                with open_unique(log_path) as log:
                    code = run_once(serial_factory, image, args, settings, number, log)
                    results.append((code, Path(log.name).resolve()))
                write_summary(summary, args, results)
                if code == 130:
                    aborted = True
                    break
        except KeyboardInterrupt:
            aborted = True
            print("ABORTED: interrupted between runs", flush=True)
        finally:
            signal.signal(signal.SIGTERM, previous)
            print(write_summary(summary, args, results), end="", flush=True)
            print(f"Report: {Path(summary.name).resolve()}", flush=True)
    if aborted:
        return 130
    if any(code == 1 for code, _ in results):
        return 1
    return 0


def main(argv=None):
    args = parse_args(argv)
    try:
        image = args.image.read_bytes()
        validate_image(image)
        check_nv_compatibility(image)
        if args.random_delay:
            timing = (f"uniform random delay {args.start_delay_ms:g}..{args.max_delay_ms:g} ms; "
                      f"{args.max_resets} reset pulses maximum; random base seed {args.random_seed}")
        else:
            attempts = math.ceil((args.max_delay_ms - args.start_delay_ms) / args.step_delay_ms) + 1
            if args.max_resets is not None:
                attempts = min(attempts, args.max_resets)
            timing = (f"delay {args.start_delay_ms:g} ms + {args.step_delay_ms:g} ms per reset, "
                      f"capped at {args.max_delay_ms:g} ms ({attempts} attempts maximum)")
        settings = (f"{len(image)} bytes; relay {args.relay_board}_{args.relay_number}; "
                    f"timing from {args.delay_from}; {timing}")
        if args.dry_run:
            print(f"DRY RUN: {args.runs} run(s); {settings}. No hardware accessed.")
            return 0
        try:
            import serial
        except ImportError as error:
            raise RuntimeError("pyserial is required; use the pwrforge Python environment") from error

        return run_repeated(serial.Serial, image, args, settings)
    except (OSError, ValueError, RuntimeError) as error:
        print(f"FAIL: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
