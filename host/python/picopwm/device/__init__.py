# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026 Ji Dong

"""Board discovery management and serialized device commands."""

from .client import DeviceError, PicoPwmHid
from .boards import BoardRegistry

__all__ = ["BoardRegistry", "DeviceError", "PicoPwmHid"]
