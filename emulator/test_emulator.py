#!/usr/bin/env python3
"""Headless smoke test for the UV-K5 emulator.

Build first with `make -C emulator`.  The test boots the real firmware logic,
checks that a non-empty LCD screenshot is produced, then performs the obfuscated
CPS hello and EEPROM-read transactions over the emulated serial PTY.
"""

import os
import select
import struct
import subprocess
import sys
import tempfile
import termios
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parent
EMU = ROOT / "build" / "uvk5-emulator"
XOR_TABLE = bytes([22, 108, 20, 230, 46, 145, 13, 64, 33, 53, 213, 64, 19, 3, 233, 128])


def xor(data: bytes) -> bytes:
    return bytes(byte ^ XOR_TABLE[i % 16] for i, byte in enumerate(data))


def crc16(data: bytes) -> int:
    crc = 0
    for byte in data:
        crc ^= byte << 8
        for _ in range(8):
            crc <<= 1
            if crc & 0x10000:
                crc = (crc ^ 0x1021) & 0xFFFF
            crc &= 0xFFFF
    return crc


def command(data: bytes) -> bytes:
    payload = data + struct.pack("<H", crc16(data))
    return struct.pack(">HBB", 0xABCD, len(data), 0) + xor(payload) + struct.pack(">H", 0xDCBA)


def read_available(fd: int, timeout: float) -> bytes:
    end = time.time() + timeout
    out = b""
    while time.time() < end:
        ready, _, _ = select.select([fd], [], [], 0.1)
        if not ready:
            continue
        try:
            chunk = os.read(fd, 4096)
        except BlockingIOError:
            continue
        if not chunk:
            break
        out += chunk
    return out


def receive_reply(fd: int, timeout: float = 2.0) -> bytes:
    end = time.time() + timeout
    data = b""
    while time.time() < end and len(data) < 4:
        data += read_available(fd, end - time.time())
    if len(data) < 4 or data[:2] != b"\xab\xcd":
        raise AssertionError(f"bad CPS reply header: {data.hex()}")
    size = data[2]
    while time.time() < end and len(data) < size + 8:
        data += read_available(fd, end - time.time())
    if len(data) < size + 8 or data[size + 6:size + 8] != b"\xdc\xba":
        raise AssertionError(f"truncated CPS reply: {data.hex()}")
    return xor(data[4:4 + size])


def open_raw_serial(path: str) -> int:
    fd = os.open(path, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
    attrs = termios.tcgetattr(fd)
    attrs[0] = attrs[1] = attrs[2] = attrs[3] = 0
    attrs[4] = attrs[5] = termios.B38400
    attrs[6][termios.VMIN] = 0
    attrs[6][termios.VTIME] = 0
    termios.tcsetattr(fd, termios.TCSANOW, attrs)
    return fd


def main() -> int:
    if not EMU.exists():
        print(f"missing emulator binary: {EMU}; run make -C emulator", file=sys.stderr)
        return 2

    with tempfile.TemporaryDirectory(prefix="uvk5-emu-test-") as td:
        tmp = Path(td)
        shot = tmp / "boot.bmp"
        eeprom = tmp / "eeprom.bin"
        uart = tmp / "uart"
        env = os.environ.copy()
        env["SDL_VIDEODRIVER"] = "dummy"
        env["SDL_AUDIODRIVER"] = "dummy"
        proc = subprocess.Popen([
            str(EMU),
            f"--eeprom={eeprom}",
            f"--uart={uart}",
            f"--screenshot={shot}",
            "--screenshot-at=6000",
        ], stdout=subprocess.PIPE, stderr=subprocess.PIPE, env=env)
        try:
            deadline = time.time() + 9
            while time.time() < deadline and not shot.exists():
                if proc.poll() is not None:
                    raise AssertionError("emulator exited before screenshot")
                time.sleep(0.1)
            if not shot.exists():
                raise AssertionError("screenshot was not produced")
            bmp = shot.read_bytes()
            if len(bmp) < 100_000 or bmp[:2] != b"BM":
                raise AssertionError("screenshot is not a populated BMP")
            if len(set(bmp[2048:8192])) < 2:
                raise AssertionError("screenshot appears blank")

            deadline = time.time() + 3
            while time.time() < deadline and not uart.exists():
                time.sleep(0.05)
            fd = open_raw_serial(str(uart))
            try:
                banner = read_available(fd, 0.4)
                if b"UV-K5 Firmware" not in banner:
                    raise AssertionError(f"missing UART boot banner: {banner!r}")

                os.write(fd, command(b"\x14\x05\x04\x00\x6a\x39\x57\x64"))
                hello = receive_reply(fd)
                if not hello.startswith(b"\x15\x05") or b"PBA v2.4" not in hello[:24]:
                    raise AssertionError(f"bad CPS hello reply: {hello.hex()}")

                readmem = b"\x1b\x05\x08\x00" + struct.pack("<HBB", 0, 32, 0) + b"\x6a\x39\x57\x64"
                os.write(fd, command(readmem))
                reply = receive_reply(fd)
                if not reply.startswith(b"\x1c\x05") or len(reply) < 40:
                    raise AssertionError(f"bad EEPROM read reply: {reply.hex()}")
                block = reply[8:40]
                frequency = struct.unpack("<I", block[:4])[0]
                if frequency != 14550000:
                    raise AssertionError(f"unexpected channel-0 frequency {frequency}")

                write_data = bytes(range(8))
                writemem = struct.pack("<HHHBBI", 0x051D, 16, 0x0100, 8, 0, 0x6457396A) + write_data
                os.write(fd, command(writemem))
                write_reply = receive_reply(fd)
                if not write_reply.startswith(b"\x1e\x05"):
                    raise AssertionError(f"bad EEPROM write reply: {write_reply.hex()}")

                readback = b"\x1b\x05\x08\x00" + struct.pack("<HBB", 0x0100, 8, 0) + b"\x6a\x39\x57\x64"
                os.write(fd, command(readback))
                verify = receive_reply(fd)
                if verify[8:16] != write_data:
                    raise AssertionError(f"EEPROM write did not round-trip: {verify.hex()}")
            finally:
                os.close(fd)
        finally:
            proc.terminate()
            try:
                proc.wait(timeout=2)
            except subprocess.TimeoutExpired:
                proc.kill()
                proc.wait()

    print("emulator smoke test passed: boot LCD, memory image, UART hello, EEPROM read/write")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
