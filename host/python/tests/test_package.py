# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026 Ji Dong

import importlib.metadata
import runpy

import picopwm
from picopwm import DeviceError, PicoPwmHid
from picopwm.device import client


def test_package_exports_reference_the_device_implementation():
    assert client.PicoPwmHid is PicoPwmHid
    assert client.DeviceError is DeviceError


def test_version_uses_installed_metadata(monkeypatch):
    def version(name):
        assert name == "picopwm"
        return "1.2.3.dev4+gabcdef"

    monkeypatch.setattr(importlib.metadata, "version", version)
    package = runpy.run_path(picopwm.__file__, run_name="picopwm.__init__")
    assert package["__version__"] == "1.2.3.dev4+gabcdef"


def test_version_without_installed_metadata_uses_debug_fallback(monkeypatch):
    def version(name):
        raise importlib.metadata.PackageNotFoundError(name)

    monkeypatch.setattr(importlib.metadata, "version", version)
    package = runpy.run_path(picopwm.__file__, run_name="picopwm.__init__")
    assert package["__version__"] == "0.0.0-debug"
