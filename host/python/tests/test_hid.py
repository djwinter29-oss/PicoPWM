import pytest

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

    def get_feature_report(self, report_id, length):
        payload = {1: IDENTITY, 2: CONFIGURATION, 20: self.result}.get(report_id, BANK)
        return bytes([report_id]) + payload

    def send_feature_report(self, data):
        self.sent.append(bytes(data))
        if self.mode == "drop":
            return len(data)
        status = 6 if self.mode == "pending" else 0
        self.result = bytes([1, data[2], data[3], status] + [0] * 8)
        return len(data)

    def close(self):
        pass


def make(mode="ok"):
    device = FakeDevice(mode)
    clock = iter(range(0, 1000))
    return device, PicoPwmHid(lambda: device, sleep=lambda s: None, monotonic=lambda: next(clock))


def test_decoders():
    identity = p.decode_identity(IDENTITY)
    assert identity["mcu"] == "RP2350" and identity["serial_number"] == "0102030405060708"
    config = p.decode_configuration(CONFIGURATION)
    assert config["system_clock_hz"] == 150_000_000 and config["target_dirty"]
    assert p.decode_bank_status(BANK, 1)[0] == {"channel": 8, "freq_hz": 1000, "duty": 50}


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
