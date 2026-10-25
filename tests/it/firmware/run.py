#!/usr/bin/env python3
"""Host checks for UART upload control flow and matching packaged BIN/ELF/HEX."""
from pathlib import Path
import struct
import sys
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
from pack_firmware import pack, calculate_hash, APP_ADDRESS, HEADER_SIZE, BANK_SIZE
from uart_dfu import (capture_recovery, check_nv_compatibility, dfu_commands,
                      exchange, upload, validate_image)


class Port:
    def __init__(self, responses=(), fallback=b"OK\n"):
        self.responses = iter(responses)
        self.fallback = fallback
        self.pending = b""
        self.timeout = 0.01
        self.sent = []

    def write(self, frame):
        self.sent.append(frame)
        return len(frame)

    def flush(self):
        pass

    def reset_input_buffer(self):
        pass

    def readline(self):
        return next(self.responses, self.fallback)

    @property
    def in_waiting(self):
        return len(self.pending)

    def read(self, size):
        if not self.pending:
            self.pending = next(self.responses, self.fallback)
        data, self.pending = self.pending[:size], self.pending[size:]
        return data


class FirmwareTests(unittest.TestCase):
    def setUp(self):
        self.image = pack(struct.pack("<II", 0x20005000, APP_ADDRESS + 9) + b"x" * 1300, (1, 2, 3))

    def test_original_patch_image_hash_vectors(self):
        self.assertEqual(calculate_hash(b"\x01\x02\x03", 0xbeef).hex(),
                         "178f0e6809f57f371bcc9e610e36ad88")
        self.assertEqual(calculate_hash(b"cdefgh", 0x6261).hex(),
                         "e8dc4081b13434b45189a720b77b6818")

    def test_known_legacy_application_is_blocked_before_any_uart_writes(self):
        image = (ROOT / "tests/data/firmware1_.bin").read_bytes()
        validate_image(image)  # Integrity is valid; the NV ABI is obsolete.
        port = Port()
        with self.assertRaisesRegex(ValueError, "old NV format"):
            upload(port, image, True, 0.1)
        self.assertEqual(port.sent, [])
        check_nv_compatibility(self.image)

    def test_corruption_rejected_before_transfer(self):
        for index in (0, 4, 8, 24, 26, 30, 34, 100, 520):
            damaged = bytearray(self.image)
            damaged[index] ^= 1
            with self.assertRaises(ValueError):
                validate_image(damaged)
        with self.assertRaises(ValueError):
            validate_image(self.image[:-1])

    def test_frame_bounds_and_header_last(self):
        for payload_size in (9, 512, 513, BANK_SIZE - HEADER_SIZE):
            image = pack(struct.pack("<II", 0x20005000, APP_ADDRESS + 9) +
                         b"x" * (payload_size - 8), (0, 0, 1))
            commands = list(dfu_commands(image))
            self.assertTrue(all(len(command) <= 1088 for command in commands))
            self.assertEqual(commands[-2:], ["AT+DFUWR=0", "AT+DFUCL"])

    @patch("uart_dfu.time.sleep")
    def test_activation_and_recovery_order(self, sleep):
        for activate in (False, True):
            port = Port()
            upload(port, self.image, activate, 0.1)
            self.assertEqual(port.sent[:4], [b"AT\r\n", b"AT+RAA=11\r\n",
                                             b"AT+RESET\r\n", b"AT\r\n"])
            if activate:
                self.assertEqual(port.sent[-2:], [b"AT+RAA=2\r\n", b"AT+RESET\r\n"])
            else:
                self.assertEqual(port.sent[-1], b"AT+DFUCL\r\n")

    @patch("uart_dfu.time.sleep")
    def test_device_error_does_not_activate(self, sleep):
        port = Port([b"OK\n"] * 4 + [b"ERROR\n"])
        with self.assertRaises(RuntimeError):
            upload(port, self.image, True, 0.1)
        self.assertNotIn(b"AT+RAA=2\r\n", port.sent)

    def test_timeout_and_startup_banner(self):
        with self.assertRaises(TimeoutError):
            exchange(Port(), "AT", 0)
        exchange(Port([b"FW 0.0.1 running\r\n", b"FW confirmed\r\n", b"OK\n"]), "AT", 0.1)

    def test_acknowledgement_split_across_serial_timeouts(self):
        port = Port([b"O", b"", b"K", b"\r", b"", b"\n"], fallback=b"")
        exchange(port, "AT+DFUWR=2,AB", 0.02)
        self.assertEqual(port.sent, [b"AT+DFUWR=2,AB\r\n"])
        self.assertEqual(port.timeout, 0.01)

    def test_fragmented_error_rejected_without_waiting_for_another_reply(self):
        port = Port([b"ER", b"", b"RO", b"R\n"], fallback=b"")
        with self.assertRaisesRegex(RuntimeError, "device rejected"):
            exchange(port, "AT+DFUWR=2,AB", 0.02)

    def test_dfu_rejection_diagnostics_are_reported_before_error(self):
        port = Port([b"BL FW2 INVALID\r\nBL FW2 REJECTED INTEGRITY\r\nERROR\n"])
        messages = []
        with self.assertRaisesRegex(RuntimeError, "device rejected AT\\+DFUCL"):
            exchange(port, "AT+DFUCL", 0.1, report=messages.append)
        self.assertEqual(messages, ["RX BL FW2 INVALID", "RX BL FW2 REJECTED INTEGRITY", "RX ERROR"])

    @patch("uart_dfu.time.sleep")
    @patch("uart_dfu.capture_recovery")
    def test_rejected_update_mode_resets_once_before_upload(self, recovery, sleep):
        port = Port([b"OK\n", b"ERROR\n", b"OK\n", b"OK\n"])
        messages = []
        upload(port, self.image, False, 0.1, messages.append)
        self.assertEqual(port.sent[:8], [b"AT\r\n", b"AT+RAA=11\r\n", b"AT+RESET\r\n",
                                         b"AT\r\n", b"AT+RAA=11\r\n", b"AT+RESET\r\n",
                                         b"AT\r\n", b"AT+DFUOP=2\r\n"])
        self.assertEqual(port.sent[-1], b"AT+DFUCL\r\n")
        recovery.assert_called_once()
        self.assertTrue(any("rejected before upload" in line for line in messages))

    @patch("uart_dfu.capture_recovery")
    def test_persistent_update_mode_rejection_never_writes_firmware(self, recovery):
        port = Port([b"OK\n", b"ERROR\n", b"OK\n", b"ERROR\n"])
        with self.assertRaisesRegex(RuntimeError, "still rejected after one recovery reset"):
            upload(port, self.image, True, 0.1, lambda message: None)
        self.assertEqual(port.sent, [b"AT\r\n", b"AT+RAA=11\r\n", b"AT+RESET\r\n",
                                     b"AT\r\n", b"AT+RAA=11\r\n"])
        recovery.assert_called_once()

    @patch("uart_dfu.capture_recovery")
    def test_missing_mode_reply_does_not_trigger_recovery_or_transfer(self, recovery):
        port = Port([b"OK\n"], fallback=b"")
        with self.assertRaisesRegex(TimeoutError, "AT\\+RAA=11"):
            upload(port, self.image, False, 0.01)
        self.assertEqual(port.sent, [b"AT\r\n", b"AT+RAA=11\r\n"])
        recovery.assert_not_called()

    def test_recovery_captures_fragmented_logs_and_restores_port_timeout(self):
        port = Port([b"BL SW", b"", b"AP RESUME page=21 phase=2\r\nBL ERROR RECOVERY\r\n"], fallback=b"")
        clock = [0.0]
        original_read = port.read

        def read(size):
            data = original_read(size)
            if not data:
                clock[0] += port.timeout
            return data

        messages = []
        with patch("uart_dfu.time.monotonic", side_effect=lambda: clock[0]), patch.object(port, "read", side_effect=read):
            capture_recovery(port, 0.1, messages.append)
        self.assertEqual(messages, ["RECOVERY RX BL SWAP RESUME page=21 phase=2",
                                    "RECOVERY RX BL ERROR RECOVERY"])
        self.assertEqual(port.timeout, 0.01)



def check_artifacts(binary_path):
    image = binary_path.read_bytes()
    validate_image(image)
    elf = binary_path.with_suffix(".elf").read_bytes()
    assert elf[:6] == b"\x7fELF\x01\x01", "expected ELF32 little endian"
    phoff = struct.unpack_from("<I", elf, 28)[0]
    phsize, phnum = struct.unpack_from("<HH", elf, 42)
    bank_start = APP_ADDRESS - HEADER_SIZE
    from_elf = bytearray(len(image))
    lowest = APP_ADDRESS
    for index in range(phnum):
        kind, offset, _, address, size, _, _, _ = struct.unpack_from("<8I", elf, phoff + index * phsize)
        if kind != 1 or size == 0:
            continue
        assert bank_start <= address < address + size <= bank_start + len(image), "ELF load outside firmware image"
        lowest = min(lowest, address)
        from_elf[address - bank_start:address - bank_start + size] = elf[offset:offset + size]
    assert lowest == bank_start
    assert from_elf == image, "ELF and BIN differ"
    from_hex = bytearray(len(image))
    base = 0
    for line in binary_path.with_suffix(".hex").read_text().splitlines():
        record = bytes.fromhex(line[1:])
        assert sum(record) % 256 == 0, "HEX checksum mismatch"
        size, high, low, kind = record[:4]
        data = record[4:4 + size]
        if kind == 4:
            base = int.from_bytes(data, "big") << 16
        elif kind == 0:
            address = base + (high << 8) + low - bank_start
            assert 0 <= address < address + size <= len(image)
            from_hex[address:address + size] = data
    assert from_hex == image, "HEX and BIN differ"
    print(f"PASS: {len(image)}-byte BIN/ELF/HEX agree; load addresses are contained in FW1")


if __name__ == "__main__":
    if len(sys.argv) > 1:
        check_artifacts(Path(sys.argv.pop(1)))
    unittest.main()
