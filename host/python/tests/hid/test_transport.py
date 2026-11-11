# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026 Ji Dong

from picopwm.hid import protocol as p
from picopwm.hid import transport
from types import SimpleNamespace
from unittest.mock import Mock

import pytest


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


@pytest.mark.parametrize(("serial", "path", "expected"), [
    ("A", None, b"/dev/a"), (None, "/dev/b", b"/dev/b"),
    ("A", "/dev/a", b"/dev/a"),
])
def test_open_filters_serial_and_path(monkeypatch, serial, path, expected):
    device = Mock()
    monkeypatch.setattr(transport, "hid", SimpleNamespace(device=lambda: device))
    monkeypatch.setattr(transport, "enumerate_devices", lambda: [
        {"serial_number": "A", "path": b"/dev/a"},
        {"serial_number": "B", "path": b"/dev/b"},
    ])
    assert transport.open_device(serial, path) is device
    device.open_path.assert_called_once_with(expected)


@pytest.mark.parametrize(("devices", "message"), [
    ([], "not found"),
    ([{"path": b"a"}, {"path": b"b"}], "multiple"),
])
def test_open_rejects_missing_or_ambiguous_devices(monkeypatch, devices, message):
    monkeypatch.setattr(transport, "enumerate_devices", lambda: devices)
    with pytest.raises(RuntimeError, match=message):
        transport.open_device()


def test_open_reports_permissions_failure_with_cause(monkeypatch):
    device = Mock()
    failure = OSError("permission denied")
    device.open_path.side_effect = failure
    monkeypatch.setattr(transport, "hid", SimpleNamespace(device=lambda: device))
    monkeypatch.setattr(transport, "enumerate_devices", lambda: [{"path": b"a"}])
    with pytest.raises(RuntimeError, match="check device permissions") as error:
        transport.open_device()
    assert error.value.__cause__ is failure


def test_missing_hid_dependency_is_explicit(monkeypatch):
    monkeypatch.setattr(transport, "hid", None)
    with pytest.raises(RuntimeError, match="missing dependency"):
        transport.enumerate_devices()
