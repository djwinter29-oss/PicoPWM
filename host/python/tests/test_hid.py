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
        self.identity = IDENTITY
        self.closed = False

    def get_feature_report(self, report_id, length):
        payload = {1: self.identity, 2: CONFIGURATION, 3: struct.pack("<BII", 1, 120000, 532480),
                   4: struct.pack("<BIiHH", 1, 123, 22900, 1170, 6720), 20: self.result}.get(report_id, BANK)
        return bytes([report_id]) + payload

    def send_feature_report(self, data):
        self.sent.append(bytes(data))
        if self.mode == "drop":
            return len(data)
        status = 6 if self.mode == "pending" else 2 if self.mode == "fail-bank" and data[3] == 3 else 0
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


def test_set_channel_completes():
    device, client = make()
    client.set_channel(3, 2000, 25)
    assert device.sent[0][:5] == bytes([20, 1, 1, 1, 3])


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
    assert http.post("/api/channels/2", json={"freq_hz": 500, "duty": 101}).status_code == 400
    assert http.post("/api/channels/2", json={"freq_hz": "x", "duty": 1}).status_code == 400
    assert http.post("/api/stop", json={}).status_code == 200
    _, dropping = make("drop")
    assert create_app(dropping).test_client().post("/api/stop", json={}).status_code == 409


def modern():
    device, client = make()
    identity = bytearray(IDENTITY)
    identity[16] = 127
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
    assert p.decode_runtime(struct.pack("<BIiHH", 1, 1, -2147483648, 0, 10000))["temperature_c"] is None
    with pytest.raises(p.ProtocolError):
        p.decode_runtime(struct.pack("<BIiHH", 1, 1, 0, 0, 10001))
    with pytest.raises(p.ProtocolError):
        p.decode_memory(struct.pack("<BII", 1, 100, 99))
    _, legacy = make()
    assert legacy.status()["runtime"] is None
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


def test_scan_errors_and_duplicate_serials_are_explicit():
    from picopwm.boards import BoardRegistry

    registry = BoardRegistry(discover=lambda: [{"path": b"a", "serial_number": "same"},
                                              {"path": b"b", "serial_number": "same"}])
    assert "duplicate" in create_app(registry=registry).test_client().get("/api/boards").json["error"]


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
