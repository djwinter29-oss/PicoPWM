# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026 Ji Dong

import pytest

from picopwm.hid import protocol as p
from ..fakes import BANK, CONFIGURATION, IDENTITY


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

