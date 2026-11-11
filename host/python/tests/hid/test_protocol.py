# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026 Ji Dong

import pytest
import struct
from typing import get_type_hints

from picopwm.hid import protocol as p
from ..fakes import BANK, CONFIGURATION, IDENTITY


def test_identity_memory_fields_are_optional_and_core_fields_are_required():
    assert {"ram_reserved_bytes", "ram_total_bytes"} == p.Identity.__optional_keys__
    assert "clock_profile_mask" in p.Identity.__required_keys__
    assert get_type_hints(p.decode_identity)["return"] is p.Identity
    assert get_type_hints(p.decode_configuration)["return"] is p.Configuration
    assert get_type_hints(p.decode_pins)["return"] is p.Pins
    assert get_type_hints(p.decode_runtime)["return"] is p.Runtime


def test_decoders():
    identity = p.decode_identity(IDENTITY)
    assert identity["mcu"] == "RP2350" and identity["serial_number"] == "0102030405060708"
    config = p.decode_configuration(CONFIGURATION)
    assert config["system_clock_hz"] == 150_000_000
    assert p.decode_bank_status(BANK, 1)[0] == {"channel": 8, "freq_hz": 1000, "duty": 50}
    reboot_identity = bytearray(IDENTITY)
    reboot_identity[16] = 127
    assert p.decode_identity(bytes(reboot_identity))["can_save"]



def test_decoder_rejects_bad_layout():
    with pytest.raises(p.ProtocolError):
        p.decode_identity(IDENTITY[:-1])
    with pytest.raises(p.ProtocolError):
        p.decode_action_result(bytes([1, 1, 1, 9] + [0] * 8))



def test_encode_set_channel():
    assert p.encode_action_request(7, p.OP_SET_CHANNEL, 9, 1000, 50) == bytes([1, 7, 1, 9, 0xE8, 3, 0, 0, 50, 0, 0, 0])


def test_optional_telemetry_reports_validate_layout_and_bounds():
    assert p.decode_identity(bytes([*IDENTITY[:16], 255, IDENTITY[17]]))["can_read_pins"]
    with pytest.raises(p.ProtocolError):
        p.decode_pins(bytes([1, 48, *range(23), 26, 27, 25]))
    with pytest.raises(p.ProtocolError):
        p.decode_pins(bytes(27))
    assert p.decode_runtime(struct.pack("<BIiHH", 1, 1, -2147483648, 0, 10000))["temperature_c"] is None
    with pytest.raises(p.ProtocolError):
        p.decode_runtime(struct.pack("<BIiHH", 1, 1, 0, 0, 10001))
    with pytest.raises(p.ProtocolError):
        p.decode_memory(struct.pack("<BII", 1, 100, 99))
