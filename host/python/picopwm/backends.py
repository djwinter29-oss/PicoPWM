"""CDC and I2C transports for the PicoPWM control protocol."""

from __future__ import annotations

import re
import struct
import time
from typing import Any

from .client import ChannelState


class CdcTransport:
    """USB CDC transport using the firmware's line-oriented shell."""

    _channel_pattern = re.compile(r"CH(?P<channel>\d+): freq=(?P<freq>\d+) Hz, duty=(?P<duty>\d+)%.*pulses=(?P<pulses>\d+)")
    _set_pattern = re.compile(r"OK CH(?P<channel>\d+) freq=(?P<freq>\d+) Hz duty=(?P<duty>\d+)%")
    _status_row_pattern = re.compile(r"^\s*(?P<channel>\d+)\s+\S+\s+\S+")

    def __init__(self, port: str, baudrate: int = 115200, timeout: float = 2.0, serial_factory: Any = None):
        if serial_factory is None:
            try:
                import serial
            except ImportError as exc:
                raise RuntimeError("CDC support requires the 'cdc' extra: pip install -e '.[cdc]'") from exc
            serial_factory = serial.Serial
        self._serial = serial_factory(port, baudrate=baudrate, timeout=timeout)
        self._timeout = timeout
        self._read_until_prompt()

    def command(self, command: str) -> list[str]:
        self._serial.write((command + "\n").encode("ascii"))
        output = self._read_until_prompt()
        return [line.strip() for line in output.splitlines() if line.strip() and line.strip() != "pico>"]

    def channel_count(self) -> int:
        lines = self.command("status")
        return sum(1 for line in lines if self._status_row_pattern.match(line))

    def get_channel(self, channel: int) -> ChannelState:
        lines = self.command(f"get {channel}")
        if not lines or lines[-1].startswith("ERR"):
            raise RuntimeError(lines[-1] if lines else "CDC get returned no response")
        match = self._channel_pattern.search(lines[-1])
        if match is None:
            raise RuntimeError(f"unrecognized CDC response: {lines[-1]}")
        return ChannelState(channel, int(match["freq"]), int(match["duty"]), int(match["pulses"]))

    def set_channel(self, channel: int, frequency_hz: int, duty_percent: int) -> ChannelState:
        lines = self.command(f"set {channel} {frequency_hz} {duty_percent}")
        if not lines or lines[-1].startswith("ERR"):
            raise RuntimeError(lines[-1] if lines else "CDC set returned no response")
        if self._set_pattern.search(lines[-1]) is None:
            raise RuntimeError(f"unrecognized CDC response: {lines[-1]}")
        return self.get_channel(channel)

    def stop_all(self) -> None:
        lines = self.command("stop")
        if not lines or lines[-1].startswith("ERR"):
            raise RuntimeError(lines[-1] if lines else "CDC stop returned no response")

    def close(self) -> None:
        self._serial.close()

    def _read_until_prompt(self) -> str:
        data = bytearray()
        prompt = b"pico> "
        deadline = time.monotonic() + self._timeout
        while prompt not in data:
            if time.monotonic() > deadline:
                raise TimeoutError("timed out waiting for PicoPWM CDC prompt")
            chunk = self._serial.read(128)
            if chunk:
                data.extend(chunk)
        return bytes(data).decode("utf-8", errors="replace").replace("\r\n", "\n").replace("\r", "\n")


class I2cTransport:
    """I2C register-map transport using smbus2 or a compatible bus object."""

    REG_CHANNELS = 0x02
    REG_GET_BASE = 0x10
    REG_SET_BASE = 0x30
    REG_STOP_ALL = 0x90
    STATUS_OK = 0
    STATUS_BUSY = 1

    def __init__(self, address: int = 0x40, bus: int = 1, bus_factory: Any = None, poll_delay: float = 0.02):
        if bus_factory is None:
            try:
                from smbus2 import SMBus
            except ImportError as exc:
                raise RuntimeError("I2C support requires the 'i2c' extra: pip install -e '.[i2c]'") from exc
            bus_factory = SMBus
        self._address = address
        self._bus = bus_factory(bus)
        self._poll_delay = poll_delay

    def channel_count(self) -> int:
        return self._read(self.REG_CHANNELS, 1)[0]

    def get_channel(self, channel: int) -> ChannelState:
        data = self._read(self.REG_GET_BASE + channel, 9)
        if len(data) == 1:
            raise RuntimeError(f"channel {channel} unavailable (status {data[0]})")
        frequency, duty, pulses = struct.unpack_from("<IBI", bytes(data))
        return ChannelState(channel, frequency, duty, pulses)

    def set_channel(self, channel: int, frequency_hz: int, duty_percent: int) -> ChannelState:
        register = self.REG_SET_BASE + channel
        self._bus.write_i2c_block_data(self._address, register, list(struct.pack("<IB", frequency_hz, duty_percent)))
        self._wait_for_status(register)
        return self.get_channel(channel)

    def stop_all(self) -> None:
        self._bus.write_byte(self._address, self.REG_STOP_ALL)
        self._wait_for_status(self.REG_STOP_ALL)

    def close(self) -> None:
        self._bus.close()

    def _read(self, register: int, length: int) -> list[int]:
        self._bus.write_byte(self._address, register)
        return list(self._bus.read_i2c_block_data(self._address, register, length))

    def _read_status(self) -> int:
        # A pure read. Rewriting the register would execute a one-byte command again
        # and, for a channel set, would be only the first byte of a six-byte write.
        return int(self._bus.read_byte(self._address))

    def _wait_for_status(self, register: int) -> None:
        deadline = time.monotonic() + 2.0
        while True:
            status = self._read_status()
            if status != self.STATUS_BUSY:
                if status != self.STATUS_OK:
                    raise RuntimeError(f"I2C command 0x{register:02x} failed with status {status}")
                return
            if time.monotonic() >= deadline:
                raise TimeoutError("timed out waiting for PicoPWM I2C command")
            time.sleep(self._poll_delay)
