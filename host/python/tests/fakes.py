# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026 Ji Dong

"""Shared in-memory HID reports and device doubles."""

import struct

from picopwm.hid import protocol as p
from picopwm.device.client import PicoPwmHid

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



def modern():
    device, client = make()
    identity = bytearray(IDENTITY)
    identity[16] = 255
    device.identity = bytes(identity)
    return device, client

