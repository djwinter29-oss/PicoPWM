# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026 Ji Dong

import struct

import pytest

from picopwm.device.client import DeviceError
from picopwm.hid import protocol as p
from ..fakes import IDENTITY, make, modern


def test_status_reads_all_banks():
    _, client = make()
    state = client.status()
    assert state["connected"] and len(state["channels"]) == 24 and state["channels"][23]["freq_hz"] == 1000



def test_set_channel_completes():
    device, client = make()
    client.set_channel(3, 2000, 25)
    assert device.sent[0][:5] == bytes([20, 1, 1, 1, 3])



def test_invalid_result_is_reconciled_when_readback_matches_requested_channel():
    device, client = make("invalid-applied")
    state = client.set_channel(0, 2000, 25)
    assert state["channels"][0] == {"channel": 0, "freq_hz": 2000, "duty": 25}



def test_invalid_result_remains_an_error_when_readback_does_not_match():
    _, client = make("invalid-channel")
    with pytest.raises(DeviceError, match=r"command failed: INVALID .*readback 1000 Hz / 50%"):
        client.set_channel(0, 2000, 25)



def test_dropped_set_is_reported():
    _, client = make("drop")
    with pytest.raises(DeviceError, match="no change was made"):
        client.set_channel(0, 1, 1)



def test_pending_times_out_as_unconfirmed():
    _, client = make("pending")
    with pytest.raises(DeviceError, match="unconfirmed"):
        client.set_channel(0, 1, 1)



def test_validation_and_overclock_ack():
    _, client = make()
    with pytest.raises(ValueError):
        client.set_channel(24, 1, 1)
    with pytest.raises(ValueError):
        client.set_clock_profile(3, False)
    client.set_clock_profile(3, True)



def test_telemetry_and_optional_report_validation():
    _, client = modern()
    state = client.status()
    assert state["identity"]["board_model"] == "Pico 2"
    assert state["identity"]["ram_total_bytes"] == 532480
    assert state["runtime"] == {"uptime_seconds": 123, "temperature_c": 22.9,
                                "core0_load": 11.7, "core1_load": 67.2}
    assert state["configuration"]["led_on"]
    assert state["pins"] == {"channels": list(range(24)), "i2c_sda": 26, "i2c_scl": 27, "led": None}
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
    _, legacy = make()
    assert legacy.status()["runtime"] is None
    assert legacy.status()["pins"] is None
    with pytest.raises(DeviceError, match="capability"):
        legacy.set_led(True)



def test_save_and_reboot_validates_before_writes_and_orders_commands():
    device, client = modern()
    with pytest.raises(ValueError):
        client.save_and_reboot(2, 64, [0, 1, 1], False)
    with pytest.raises(ValueError):
        client.save_and_reboot(0, 120, [0, 1, 1], False)
    with pytest.raises(ValueError):
        client.save_and_reboot(0, 64, [0, True, 1], False)
    assert not device.sent
    assert client.save_and_reboot(2, 64, [0, 1, 1], True) == {"rebooting": True}
    assert [data[3] for data in device.sent] == [6, 4, 3, 3, 3, 7]
    assert device.sent[-1][10] == 3
    assert device.closed



def test_save_stops_at_first_failure_and_reboot_never_saves():
    device, client = modern()
    device.mode = "fail-bank"
    with pytest.raises(DeviceError, match="target settings may have changed"):
        client.save_and_reboot(0, 64, [0, 1, 1], False)
    assert [data[3] for data in device.sent] == [6, 4, 3]
    device, client = modern()
    client.reboot()
    assert [data[3] for data in device.sent] == [9]
    assert device.sent[0][10] == p.FLAG_CONFIRM_SAVE

