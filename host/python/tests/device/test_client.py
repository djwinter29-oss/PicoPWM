# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026 Ji Dong

import pytest
import threading
from concurrent.futures import ThreadPoolExecutor
from unittest.mock import Mock

from picopwm.device.client import DeviceError, PicoPwmHid
from picopwm.hid import protocol as p
from ..fakes import make, modern


def test_status_reads_all_banks():
    _, client = make()
    state = client.status()
    assert state["connected"] and len(state["channels"]) == 24 and state["channels"][23]["freq_hz"] == 1000


def test_status_snapshots_do_not_expose_cached_state():
    _, client = modern()
    first = client.status()
    second = client.status()
    first["identity"]["can_set_led"] = False
    first["pins"]["channels"][0] = 47
    first["configuration"]["target"]["roles"][0] = 1
    first["channels"][0]["freq_hz"] = 9999
    assert second["identity"]["can_set_led"]
    assert second["pins"]["channels"][0] == 0
    assert second["configuration"]["target"]["roles"][0] == 0
    assert second["channels"][0]["freq_hz"] == 1000
    assert client.status() == second
    assert client.set_led(True)["identity"]["can_set_led"]


@pytest.mark.parametrize("operation", [
    p.OP_SET_CLOCK_PROFILE, p.OP_SET_I2C_ADDRESS, p.OP_SET_BANK, p.OP_SAVE_REBOOT,
])
@pytest.mark.parametrize("failure", [
    DeviceError("firmware rejected command", status=p.STATUS_INVALID),
    OSError("transfer failed"),
])
def test_save_and_reboot_preserves_error_status_and_cause(monkeypatch, operation, failure):
    device, client = modern()
    perform = client._perform

    def fail(selected, **fields):
        if selected == operation:
            raise failure
        return perform(selected, **fields)

    monkeypatch.setattr(client, "_perform", fail)
    with pytest.raises(DeviceError, match="target settings may have changed") as error:
        client.save_and_reboot(0, 64, [0, 1, 1], False)
    assert error.value.status == (failure.status if isinstance(failure, DeviceError) else None)
    if isinstance(failure, DeviceError):
        assert error.value.__cause__ is failure
    elif operation == p.OP_SAVE_REBOOT:
        assert error.value.__cause__.__cause__ is failure
    else:
        assert error.value.__cause__ is failure
    assert device.closed



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


@pytest.mark.parametrize(("method", "args"), [
    ("set_channel", (True, 1000, 50)), ("set_channel", (0, 1000.5, 50)),
    ("set_channel", (0, 1000, False)), ("set_channel", ("0", 1000, 50)),
    ("set_bank", (False, 0, 1)), ("set_bank", (0, 1.0, 1)),
    ("set_bank", (0, 0, True)), ("set_i2c_address", (True,)),
    ("set_i2c_address", (128,)), ("set_clock_profile", (False, False)),
    ("set_clock_profile", (0, 1)), ("set_clock_profile", (3, False)),
    ("save_configuration", (1,)), ("set_led", (2,)),
    ("set_led", ("yes",)), ("save_and_reboot", (0, True, [0, 1, 1], False)),
    ("save_and_reboot", (0, 64, [0, True, 1], False)),
    ("save_and_reboot", (0, 64, None, False)),
    ("save_and_reboot", (0, 64, [0, 1, 1], 1)),
])
def test_invalid_public_arguments_are_rejected_before_io(method, args):
    opener = Mock(side_effect=AssertionError("invalid input must not open the device"))
    client = PicoPwmHid(opener)
    with pytest.raises(ValueError):
        getattr(client, method)(*args)
    opener.assert_not_called()


@pytest.mark.parametrize("method,args", [
    ("set_clock_profile", (0, False)),
    ("save_and_reboot", (0, 64, [0, 1, 1], False)),
])
def test_identity_failure_is_a_device_error(method, args):
    def missing():
        raise RuntimeError("interface not found")

    with pytest.raises(DeviceError, match="Unable to read device identity") as error:
        getattr(PicoPwmHid(missing), method)(*args)
    assert error.value.status is None


def test_rejected_command_exposes_firmware_status():
    _, client = make("invalid-channel")
    with pytest.raises(DeviceError) as error:
        client.set_channel(0, 2000, 25)
    assert error.value.status == p.STATUS_INVALID


def test_reconciliation_uses_status_code_not_exception_text(monkeypatch):
    _, client = make()

    def rejected(*args, **kwargs):
        raise DeviceError("a differently worded rejection", status=p.STATUS_INVALID)

    monkeypatch.setattr(client, "_run", rejected)
    state = client.set_channel(0, 1000, 50)
    assert state["channels"][0] == {"channel": 0, "freq_hz": 1000, "duty": 50}


def test_close_failure_is_logged_and_state_is_cleared(caplog):
    device, client = make()
    client.status()
    device.close = Mock(side_effect=OSError("close failed"))
    client.close()
    assert "Failed to close HID device" in caplog.text
    assert client._device is None
    assert client._identity is None


@pytest.mark.parametrize("operation", ["save", "clock", "reconcile"])
def test_logical_operation_keeps_lock_until_validation_and_readback_complete(monkeypatch, operation):
    _, client = make("invalid-applied" if operation == "reconcile" else "ok")
    paused = threading.Event()
    attempted = threading.Event()
    blocked = []
    lock = threading.RLock()

    class ObservedLock:
        def __enter__(self):
            if threading.current_thread().name == "contender":
                acquired = lock.acquire(blocking=False)
                blocked.append(not acquired)
                attempted.set()
                if not acquired:
                    lock.acquire()
            else:
                lock.acquire()

        def __exit__(self, *args):
            lock.release()

    client._lock = ObservedLock()

    def pause():
        paused.set()
        assert attempted.wait(5), "competing command did not attempt the lock"

    original = client._run

    def run(op, *args, **kwargs):
        if operation != "reconcile" and op != p.OP_STOP:
            pause()
        try:
            return original(op, *args, **kwargs)
        except DeviceError:
            if operation == "reconcile" and op == p.OP_SET_CHANNEL:
                pause()
            raise

    monkeypatch.setattr(client, "_run", run)
    command = {
        "save": lambda: client.save_configuration(False),
        "clock": lambda: client.set_clock_profile(0, False),
        "reconcile": lambda: client.set_channel(0, 2000, 25),
    }[operation]

    def competing_command():
        threading.current_thread().name = "contender"
        assert paused.wait(5), "operation did not reach the validation/readback boundary"
        client.stop()

    with ThreadPoolExecutor(max_workers=2) as pool:
        first = pool.submit(command)
        second = pool.submit(competing_command)
        first.result(timeout=10)
        second.result(timeout=10)
    assert blocked[0]
