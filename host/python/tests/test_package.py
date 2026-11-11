# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026 Ji Dong

from picopwm import DeviceError, PicoPwmHid
from picopwm.device import client


def test_package_exports_reference_the_device_implementation():
    assert client.PicoPwmHid is PicoPwmHid
    assert client.DeviceError is DeviceError

