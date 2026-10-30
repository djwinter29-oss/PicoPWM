# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026 Ji Dong

import pytest
import struct

from picopwm import protocol as p
from picopwm.app import create_app
from picopwm.client import DeviceError, PicoPwmHid

IDENTITY = bytes([1, 3, 0, 1, 2, 3, 4, 5, 6, 7, 8, 1, 2, 3, 2, 24, 3, 0x1F])
CONFIGURATION = bytes([1, 0, 1, 2, 0, 0, 1, 0x20, 0, 1, 2, 0, 0, 1, 0x20, 1, 1, 0x80, 0xD1, 0xF0, 0x08])
BANK = bytes([1] + [0xE8, 0x03, 0, 0, 50] * 8)


class FakeDevice:
    """Serves reports; mode 'ok' completes the SET, 'drop' ignores it, 'pending' never finishes."""

    def __init__(self, mode="ok"):
        self.mode = mode
        self.result = bytes([1, 0, 0, 0] + [0] * 8)
        self.sent = []
        self.channels = [{"freq_hz": 1000, "duty": 50} for _ in range(p.CHANNEL_COUNT)]
        self.identity = IDENTITY
        self.closed = False

    def get_feature_report(self, report_id, length):
        if p.BANK_STATUS_REPORT <= report_id < p.BANK_STATUS_REPORT + p.BANK_COUNT:
            start = (report_id - p.BANK_STATUS_REPORT) * p.BANK_SIZE
            payload = bytes([1]) + b"".join(
                struct.pack("<IB", entry["freq_hz"], entry["duty"])
                for entry in self.channels[start:start + p.BANK_SIZE])
        else:
            payload = {1: self.identity, 2: CONFIGURATION, 3: struct.pack("<BII", 1, 120000, 532480),
                       4: struct.pack("<BIiHH", 1, 123, 22900, 1170, 6720),
                       5: bytes([1, *range(24), 26, 27, 255]), 20: self.result}.get(report_id, BANK)
        return bytes([report_id]) + payload

    def send_feature_report(self, data):
        self.sent.append(bytes(data))
        if self.mode == "drop":
            return len(data)
        status = 6 if self.mode == "pending" else 2 if self.mode == "fail-bank" and data[3] == 3 else 0
        if self.mode == "invalid-channel" and data[3] == p.OP_SET_CHANNEL:
            status = 2
        if self.mode == "invalid-applied" and data[3] == p.OP_SET_CHANNEL:
            channel = data[4]
            self.channels[channel] = {"freq_hz": struct.unpack_from("<I", data, 5)[0], "duty": data[9]}
            status = 2
        self.result = bytes([1, data[2], data[3], status] + [0] * 8)
        return len(data)

    def close(self):
        self.closed = True


def make(mode="ok"):
    device = FakeDevice(mode)
    clock = iter(range(0, 1000))
    return device, PicoPwmHid(lambda: device, sleep=lambda s: None, monotonic=lambda: next(clock))


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


def test_status_reads_all_banks():
    _, client = make()
    state = client.status()
    assert state["connected"] and len(state["channels"]) == 24 and state["channels"][23]["freq_hz"] == 1000


def test_status_without_device():
    def opener():
        raise RuntimeError("PicoPWM HID interface not found")

    state = PicoPwmHid(opener).status()
    assert not state["connected"] and "not found" in state["error"]
    response = create_app(PicoPwmHid(opener)).test_client().get("/api/status")
    assert response.json["error"] == "Device status unavailable"


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


def test_api():
    _, client = make()
    http = create_app(client).test_client()
    assert http.get("/api/status").json["connected"]
    assert http.post("/api/channels/2", json={"freq_hz": 500, "duty": 10}).status_code == 200
    invalid_duty = http.post("/api/channels/2", json={"freq_hz": 500, "duty": 101})
    assert invalid_duty.status_code == 400
    assert invalid_duty.json == {"error": "Invalid request fields"}
    assert http.post("/api/channels/2", json={"freq_hz": "x", "duty": 1}).status_code == 400
    assert http.post("/api/stop", json={}).status_code == 200
    _, dropping = make("drop")
    assert create_app(dropping).test_client().post("/api/stop", json={}).status_code == 409


def test_api_does_not_expose_device_exception_details():
    private_detail = "private device path /secret/device"

    class FailedClient:
        def status(self):
            return {"connected": False, "error": private_detail, "identity_error": private_detail,
                    "configuration_error": None, "runtime_error": None, "pins_error": None,
                    "bank_errors": [private_detail, None, None]}

        def stop(self):
            raise OSError(private_detail)

    http = create_app(FailedClient()).test_client()
    status = http.get("/api/status")
    assert status.json["error"] == "Device status unavailable"
    assert status.json["identity_error"] == "Device status unavailable"
    assert status.json["bank_errors"] == ["Device status unavailable", None, None]
    assert private_detail not in status.get_data(as_text=True)

    action = http.post("/api/stop", json={})
    assert action.status_code == 503
    assert action.json == {"error": "Device operation failed"}
    assert private_detail not in action.get_data(as_text=True)


@pytest.mark.parametrize(("failure", "status_code", "message"), [
    (p.ProtocolError("private protocol detail"), 409, "Invalid response from device"),
    (DeviceError("private device detail"), 409, "Device request failed"),
    (OSError("private operating system detail"), 503, "Device operation failed"),
    (RuntimeError("private runtime detail"), 503, "Device operation failed"),
])
def test_api_action_exceptions_are_not_returned(failure, status_code, message):
    class FailedClient:
        def stop(self):
            raise failure

    response = create_app(FailedClient()).test_client().post("/api/stop", json={})
    assert response.status_code == status_code
    assert response.json == {"error": message}
    assert str(failure) not in response.get_data(as_text=True)


@pytest.mark.parametrize("failure", [
    OSError("private operating system detail"),
    RuntimeError("private runtime detail"),
])
def test_board_discovery_exceptions_are_not_returned(failure):
    from picopwm.boards import BoardRegistry

    def discover():
        raise failure

    response = create_app(registry=BoardRegistry(discover=discover)).test_client().get("/api/boards")
    assert response.status_code == 503
    assert response.json == {"error": "Board discovery failed"}
    assert str(failure) not in response.get_data(as_text=True)


def test_status_exception_is_not_returned():
    private_detail = "private device path /secret/device"

    class FailedClient:
        def status(self):
            raise DeviceError(private_detail)

    response = create_app(FailedClient()).test_client().get("/api/status")
    assert response.status_code == 409
    assert response.json == {"error": "Device request failed"}
    assert private_detail not in response.get_data(as_text=True)


def modern():
    device, client = make()
    identity = bytearray(IDENTITY)
    identity[16] = 255
    device.identity = bytes(identity)
    return device, client


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


def test_pin_report_errors_are_explicit_and_disconnect_clears_the_map():
    device, client = modern()
    original = device.get_feature_report

    def invalid_pins(report_id, length):
        if report_id == p.PINS_REPORT:
            return bytes([p.PINS_REPORT, 1, 48, *range(23), 26, 27, 25])
        return original(report_id, length)

    device.get_feature_report = invalid_pins
    state = client.status()
    assert state["connected"] and state["pins"] is None
    assert "invalid pin" in state["pins_error"]
    response = create_app(client).test_client().get("/api/status")
    assert response.json["pins_error"] == "Device status unavailable"
    device.get_feature_report = original
    assert client.status()["pins"]["channels"][23] == 23

    def unplugged(report_id, length):
        raise OSError("device unplugged")

    device.get_feature_report = unplugged
    state = client.status()
    assert not state["connected"] and state["pins"] is None

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


def test_board_registry_and_routes_keep_selection_pinned():
    from picopwm.boards import BoardRegistry

    discovered = [{"path": b"/dev/a", "serial_number": "A", "product_string": "PicoPWM"},
                  {"path": b"/dev/b", "serial_number": "B", "product_string": "PicoPWM"}]
    devices = {}

    def factory(board):
        devices[board["serial_number"]] = modern()
        return devices[board["serial_number"]][1]

    registry = BoardRegistry(discover=lambda: discovered, factory=factory, serial="B")
    http = create_app(registry=registry).test_client()
    scan = http.get("/api/boards").json
    a, b = [board["id"] for board in scan["boards"]]
    assert scan["default_board_id"] == b
    assert not http.get("/api/status").json["connected"]
    assert http.get("/api/status?board=unknown").status_code == 409
    assert http.get(f"/api/status?board={a}").json["runtime"]["temperature_c"] == 22.9
    assert http.post(f"/api/led?board={b}", json={"on": True}).status_code == 200
    assert devices["A"][0].sent == []
    assert devices["B"][0].sent[-1][3] == 8
    assert http.post(f"/api/led?board={b}", json={"on": 1}).status_code == 400
    assert http.post(f"/api/save-reboot?board={b}", json={
        "profile": 0, "address": 64, "roles": [0, 1, 1]}).status_code == 200
    assert http.post("/api/reboot", json={}).status_code == 409
    assert http.get("/api/host").json["dashboard_type"] == "Python"
    for path in ["/", "/css/webhid.css", "/js/view.mjs", "/js/device.mjs", "/js/client.mjs"]:
        assert http.get(path).status_code == 200
    discovered[0]["path"] = b"/dev/new"
    assert registry.boards()["boards"][0]["id"] == a
    assert registry.client(a) is devices["A"][1]
    discovered[:] = []
    assert not registry.boards()["boards"]
    assert registry.client(a) is devices["A"][1]
    registry.close()
    assert devices["A"][0].closed


def test_scan_errors_are_generic_and_logged(caplog):
    from picopwm.boards import BoardRegistry

    registry = BoardRegistry(discover=lambda: [{"path": b"a", "serial_number": "same"},
                                              {"path": b"b", "serial_number": "same"}])
    response = create_app(registry=registry).test_client().get("/api/boards")
    assert response.status_code == 503
    assert response.json == {"error": "Board discovery failed"}
    assert "duplicate PicoPWM board identity" in caplog.text


def test_bind_and_dashboard_reject_remote_and_foreign_callers():
    from picopwm.app import bind_host

    assert bind_host(None, None) == "127.0.0.1"
    assert bind_host("localhost", "") == "localhost"
    assert bind_host("0.0.0.0", "secret") == "0.0.0.0"
    with pytest.raises(RuntimeError, match="loopback"):
        bind_host("0.0.0.0", "")

    _, client = make()
    open_host = create_app(client).test_client()
    assert open_host.get("/api/status", headers={"Host": "evil.example"}).status_code == 403
    assert open_host.get("/api/status").status_code == 200
    http = create_app(client, token="secret").test_client()
    assert http.get("/").status_code == 200
    assert http.get("/api/status", headers={"Origin": "http://evil.example"}).status_code == 403
    assert http.get("/api/status", headers={"Authorization": "Bearer secret",
                                            "Origin": "http://evil.example"}).status_code == 403
    assert http.get("/api/status").status_code == 401
    assert http.post("/api/login", json={"token": "nope"}).status_code == 401
    login = http.post("/api/login", json={"token": "secret"})
    assert login.status_code == 200
    assert "picopwm_auth" in login.headers.get("Set-Cookie", "")
    assert "HttpOnly" in login.headers.get("Set-Cookie", "")
    assert http.get("/api/status").status_code == 200
    other = create_app(client, token="secret").test_client()
    assert other.get("/api/status", headers={"Authorization": "Bearer secret"}).status_code == 200
    assert create_app(client).test_client().post("/api/login", json={"token": "x"}).status_code == 404


def test_discovery_matches_vendor_usage_or_constrained_picouart_style_fallback(monkeypatch):
    from picopwm import transport
    from types import SimpleNamespace

    devices = [
        {"path": b"usage", "usage_page": p.USAGE_PAGE, "usage": p.USAGE},
        {"path": b"fallback", "product_string": "PicoPWM USB CDC HID", "interface_number": 2},
        {"path": b"interface", "interface_number": 2},
        {"path": b"wrong-usage", "usage_page": 1, "usage": 1, "interface_number": 2},
        {"path": b"wrong-interface", "product_string": "PicoPWM USB CDC HID", "interface_number": 0},
        {"path": b"unknown", "product_string": "other", "interface_number": 2},
    ]
    monkeypatch.setattr(transport, "hid", SimpleNamespace(enumerate=lambda vid, pid: devices))
    assert [info["path"] for info in transport.enumerate_devices()] == [b"usage", b"fallback", b"interface"]
