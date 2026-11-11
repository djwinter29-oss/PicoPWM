# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026 Ji Dong

from picopwm.hid import protocol as p


def test_discovery_matches_vendor_usage_or_constrained_picouart_style_fallback(monkeypatch):
    from picopwm.hid import transport
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

