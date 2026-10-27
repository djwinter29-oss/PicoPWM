"""PicoPWM HID feature-report layouts (version 1); see docs/design/usb/hid-report-reference.md."""

from __future__ import annotations

import struct
from typing import Any

VENDOR_ID = 0xCAFE
PRODUCT_ID = 0x4012
USAGE_PAGE = 0xFF00
USAGE = 0x01
HID_PRODUCT_ID = 0x0003

IDENTITY_REPORT = 1
CONFIGURATION_REPORT = 2
MEMORY_REPORT = 3
RUNTIME_REPORT = 4
PINS_REPORT = 5
BANK_STATUS_REPORT = 10
ACTION_REPORT = 20

CHANNEL_COUNT = 24
BANK_COUNT = 3
BANK_SIZE = 8
IDENTITY_SIZE = 18
CONFIGURATION_SIZE = 21
MEMORY_SIZE = 9
RUNTIME_SIZE = 13
PINS_SIZE = 28
BANK_STATUS_SIZE = 41
ACTION_SIZE = 12

OP_SET_CHANNEL = 1
OP_STOP = 2
OP_SET_BANK = 3
OP_SET_I2C_ADDRESS = 4
OP_SAVE_CONFIG = 5
OP_SET_CLOCK_PROFILE = 6
OP_SAVE_REBOOT = 7
OP_SET_LED = 8
OP_REBOOT = 9

FLAG_CONFIRM_SAVE = 1
FLAG_ACK_OVERCLOCK = 2

STATUS_OK = 0
STATUS_PENDING = 6
STATUS_NAMES = (
    "OK", "BUSY", "INVALID", "UNAVAILABLE", "TIMEOUT", "APPLY_FAILED", "PENDING", "UNSUPPORTED", "STORAGE_FAILED",
)


class ProtocolError(ValueError):
    """A report that violates the documented layout."""


def _check(payload: bytes, size: int, name: str) -> None:
    if len(payload) != size:
        raise ProtocolError(f"{name} must contain exactly {size} payload bytes (received {len(payload)})")
    if payload[0] != 1:
        raise ProtocolError(f"unsupported {name} layout version {payload[0]}")


def decode_identity(payload: bytes) -> dict[str, Any]:
    _check(payload, IDENTITY_SIZE, "identity report")
    product_id = struct.unpack_from("<H", payload, 1)[0]
    if product_id != HID_PRODUCT_ID:
        raise ProtocolError(f"unsupported product ID 0x{product_id:04x}")
    mcu, channels, capabilities, profiles = payload[14:18]
    if (mcu not in (1, 2) or channels != CHANNEL_COUNT or profiles & ~0x1F
            or bool(capabilities & 2) != bool(profiles) or (profiles and not profiles & 2)):
        raise ProtocolError("invalid identity capabilities")
    return {
        "product_id": product_id,
        "serial_number": payload[3:11].hex().upper(),
        "firmware_version": f"{payload[11]}.{payload[12]}.{payload[13]}",
        "mcu": {1: "RP2040", 2: "RP2350"}[mcu],
        "channel_count": channels,
        "can_save": bool(capabilities & 1),
        "can_save_reboot": bool(capabilities & 4),
        "can_set_led": bool(capabilities & 8),
        "can_reboot": bool(capabilities & 16),
        "can_read_memory": bool(capabilities & 32),
        "can_read_runtime": bool(capabilities & 64),
        "can_read_pins": bool(capabilities & 128),
        "board_model": {1: "Pico", 2: "Pico 2"}[mcu],
        "can_set_clock": bool(capabilities & 2),
        "clock_profile_mask": profiles,
    }


def _bank_configuration(payload: bytes, offset: int) -> dict[str, Any]:
    backends = list(payload[offset:offset + 3])
    roles = list(payload[offset + 3:offset + 6])
    address = payload[offset + 6]
    if any(value > 2 for value in backends) or any(value > 1 for value in roles) or address > 127:
        raise ProtocolError("invalid bank configuration")
    return {"backends": backends, "roles": roles, "i2c_address": address}


def decode_configuration(payload: bytes) -> dict[str, Any]:
    _check(payload, CONFIGURATION_SIZE, "configuration report")
    profile = payload[16]
    clock = struct.unpack_from("<I", payload, 17)[0]
    if profile > 4 or not clock:
        raise ProtocolError("invalid configuration fields")
    return {
        "running": _bank_configuration(payload, 1),
        "target": _bank_configuration(payload, 8),
        "clock_profile": profile,
        "system_clock_hz": clock,
        "led_on": bool(payload[15] & 1),
    }


def decode_memory(payload: bytes) -> dict[str, int]:
    _check(payload, MEMORY_SIZE, "memory report")
    reserved, total = struct.unpack_from("<II", payload, 1)
    if not total or reserved > total:
        raise ProtocolError("invalid memory report")
    return {"ram_reserved_bytes": reserved, "ram_total_bytes": total}


def decode_runtime(payload: bytes) -> dict[str, Any]:
    _check(payload, RUNTIME_SIZE, "runtime report")
    uptime, temperature, core0, core1 = struct.unpack_from("<IiHH", payload, 1)
    if core0 > 10000 or core1 > 10000:
        raise ProtocolError("invalid runtime service load")
    return {"uptime_seconds": uptime, "temperature_c": None if temperature == -2147483648 else temperature / 1000,
            "core0_load": core0 / 100, "core1_load": core1 / 100}


def decode_pins(payload: bytes) -> dict[str, Any]:
    _check(payload, PINS_SIZE, "pin assignment report")
    if any(pin > 47 and pin != 255 for pin in payload[1:]):
        raise ProtocolError("invalid pin assignment report")
    pins = [None if pin == 255 else pin for pin in payload[1:]]
    return {"channels": pins[:24], "i2c_sda": pins[24], "i2c_scl": pins[25], "led": pins[26]}


def decode_bank_status(payload: bytes, bank: int) -> list[dict[str, int]]:
    _check(payload, BANK_STATUS_SIZE, f"bank status report {BANK_STATUS_REPORT + bank}")
    channels = []
    for index in range(BANK_SIZE):
        freq_hz, duty = struct.unpack_from("<IB", payload, 1 + index * 5)
        if duty > 100:
            raise ProtocolError("invalid duty in bank status")
        channels.append({"channel": bank * BANK_SIZE + index, "freq_hz": freq_hz, "duty": duty})
    return channels


def decode_action_result(payload: bytes) -> dict[str, Any]:
    _check(payload, ACTION_SIZE, "action result report")
    status = payload[3]
    if status >= len(STATUS_NAMES) or any(payload[4:]):
        raise ProtocolError("invalid action result")
    return {"transaction_id": payload[1], "operation": payload[2], "status": status, "status_name": STATUS_NAMES[status]}


def encode_action_request(transaction_id: int, operation: int, channel: int = 0, arg0: int = 0, arg1: int = 0,
                          flags: int = 0) -> bytes:
    return struct.pack("<BBBBIBBH", 1, transaction_id, operation, channel, arg0, arg1, flags, 0)
