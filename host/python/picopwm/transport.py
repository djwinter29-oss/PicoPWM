# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026 Ji Dong

"""HID discovery and opening for the PicoPWM vendor interface."""

from __future__ import annotations

from typing import Any

from .protocol import PRODUCT_ID, USAGE, USAGE_PAGE, VENDOR_ID

try:
    import hid
except ImportError as error:  # hidapi is a runtime dependency; fail only when a device is opened
    hid = None
    _IMPORT_ERROR: ImportError | None = error
else:
    _IMPORT_ERROR = None


def enumerate_devices() -> list[dict[str, Any]]:
    if hid is None:
        raise RuntimeError("missing dependency: install picopwm with its runtime dependencies") from _IMPORT_ERROR
    # Match PicoUart's constrained fallback for hidapi backends without usage metadata.
    return [info for info in hid.enumerate(VENDOR_ID, PRODUCT_ID)
            if (info.get("usage_page") == USAGE_PAGE and info.get("usage") == USAGE)
            or (not info.get("usage_page") and not info.get("usage")
                and (info.get("product_string") == "PicoPWM USB CDC HID"
                     or (not info.get("product_string") and info.get("interface_number") == 2))
                and info.get("interface_number") in (None, -1, 2))]


def open_device(serial: str | None = None, path: str | None = None) -> Any:
    devices = enumerate_devices()
    if serial is not None:
        devices = [info for info in devices if info.get("serial_number") == serial]
    if path is not None:
        devices = [info for info in devices
                   if (info["path"].decode(errors="surrogateescape") if isinstance(info["path"], bytes)
                       else info["path"]) == path]
    if not devices:
        raise RuntimeError("PicoPWM HID interface not found")
    if len(devices) > 1:
        raise RuntimeError("multiple PicoPWM HID interfaces found; set PICOPWM_SERIAL or PICOPWM_DEVICE_PATH")
    device = hid.device()
    try:
        device.open_path(devices[0]["path"])
    except OSError as error:
        raise RuntimeError(f"failed to open PicoPWM HID interface; check device permissions: {error}") from error
    return device
