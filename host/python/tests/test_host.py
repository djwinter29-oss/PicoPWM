from dataclasses import replace
import struct

import pytest

from picopwm.backends import CdcTransport, I2cTransport
from picopwm.client import ChannelState, PicoPWMClient


class FakeBackend:
    def __init__(self):
        self.states = [ChannelState(channel, 0, 50, 0) for channel in range(24)]
        self.stopped = False

    def channel_count(self):
        return len(self.states)

    def get_channel(self, channel):
        return self.states[channel]

    def set_channel(self, channel, frequency_hz, duty_percent):
        self.states[channel] = replace(self.states[channel], frequency_hz=frequency_hz, duty_percent=duty_percent)
        return self.states[channel]

    def stop_all(self):
        self.stopped = True
        self.states = [replace(state, frequency_hz=0) for state in self.states]

    def close(self):
        pass


class FakeSerial:
    def __init__(self, port, baudrate, timeout):
        self.response = bytearray(b"PicoPWM\r\npico> ")
        self.commands = []

    def write(self, payload):
        command = payload.decode("ascii").strip()
        self.commands.append(command)
        if command == "status":
            rows = "\r\n".join(f"{channel:<2}  hw       OFF       0      50  0" for channel in range(24))
            self.response.extend((rows + "\r\npico> ").encode("ascii"))
        elif command.startswith("get "):
            channel = int(command.split()[1])
            self.response.extend(f"CH{channel}: freq=1200 Hz, duty=35%, pulses=9, enabled=yes\r\npico> ".encode("ascii"))
        elif command.startswith("set "):
            self.response.extend(f"OK CH{command.split()[1]} freq=800 Hz duty=25%\r\npico> ".encode("ascii"))
        return len(payload)

    def read(self, size):
        chunk = self.response[:size]
        del self.response[:size]
        return bytes(chunk)

    def close(self):
        pass


class FakeI2cBus:
    def __init__(self, number):
        self.writes = []

    def write_byte(self, address, register):
        self.writes.append(("byte", address, register))

    def write_i2c_block_data(self, address, register, payload):
        self.writes.append(("block", address, register, payload))

    def read_i2c_block_data(self, address, register, length):
        if register == I2cTransport.REG_CHANNELS:
            return [24]
        if register == I2cTransport.REG_GET_BASE + 3:
            return list(struct.pack("<IBI", 1200, 35, 9))
        return [I2cTransport.STATUS_OK]

    def read_byte(self, address):
        self.writes.append(("read", address))
        return I2cTransport.STATUS_OK

    def close(self):
        pass


def test_client_controls_all_channels():
    backend = FakeBackend()
    client = PicoPWMClient(backend)

    assert client.channel_count == 24
    state = client.set_channel(7, 1200, 35)

    assert state == ChannelState(7, 1200, 35, 0)
    assert client.channel(7).enabled


def test_client_rejects_invalid_values():
    client = PicoPWMClient(FakeBackend())

    with pytest.raises(ValueError):
        client.set_channel(24, 100, 50)
    with pytest.raises(ValueError):
        client.set_channel(0, -1, 50)
    with pytest.raises(ValueError):
        client.set_channel(0, 100, 101)


def test_cdc_transport_parses_status_and_channel_responses():
    transport = CdcTransport("/dev/test", serial_factory=FakeSerial)

    assert transport.channel_count() == 24
    assert transport.get_channel(3) == ChannelState(3, 1200, 35, 9)
    assert transport.set_channel(3, 800, 25) == ChannelState(3, 1200, 35, 9)


def test_i2c_transport_uses_protocol_layout():
    transport = I2cTransport(bus_factory=FakeI2cBus)

    assert transport.channel_count() == 24
    assert transport.get_channel(3) == ChannelState(3, 1200, 35, 9)
    transport.set_channel(3, 800, 25)
    assert ("block", 0x40, 0x33, list(struct.pack("<IB", 800, 25))) in transport._bus.writes
    assert ("byte", 0x40, 0x33) not in transport._bus.writes
    assert ("read", 0x40) in transport._bus.writes
    transport.stop_all()
    assert transport._bus.writes.count(("byte", 0x40, I2cTransport.REG_STOP_ALL)) == 1


def test_flask_dashboard_updates_channel_and_stops():
    pytest.importorskip("flask")
    from picopwm.app import create_app

    backend = FakeBackend()
    app = create_app(PicoPWMClient(backend))
    app.config.update(TESTING=True)
    http = app.test_client()

    response = http.get("/")
    assert response.status_code == 200
    assert b"Channel desk" in response.data
    assert response.data.count(b"Apply") == 24

    response = http.post("/channels/3", data={"frequency_hz": "800", "duty_percent": "25"})
    assert response.status_code == 302
    assert backend.get_channel(3).frequency_hz == 800

    response = http.post("/stop")
    assert response.status_code == 302
    assert backend.stopped
