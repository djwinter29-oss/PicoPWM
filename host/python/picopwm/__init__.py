# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026 Ji Dong

"""Host-side tools for PicoPWM."""

from importlib.metadata import PackageNotFoundError, version

from .device.client import DeviceError, PicoPwmHid

try:
    __version__ = version("picopwm")
except PackageNotFoundError:
    __version__ = "0.0.0-debug"

__all__ = ["DeviceError", "PicoPwmHid", "__version__"]
