# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026 Ji Dong

"""Page-local board selection with shared, serialized HID clients."""

import hashlib
import os
import threading
from typing import Any, Callable, TypedDict

from .client import DeviceError, PicoPwmHid
from ..hid.transport import enumerate_devices, open_device


class BoardInfo(TypedDict):
    """Discovered board identity and the path used when no serial is available."""

    id: str
    product_name: str
    serial_number: str | None
    device_path: str


class BoardScan(TypedDict):
    """Current discovery results and the preferred page selection."""

    boards: list[BoardInfo]
    default_board_id: str | None


class BoardRegistry:
    """Keep requests pinned to a discovered board, including across USB reconnects."""

    def __init__(self, discover: Callable[[], list[dict[str, Any]]] = enumerate_devices,
                 factory: Callable[[BoardInfo], PicoPwmHid] | None = None,
                 serial: str | None = None, path: str | None = None):
        self._discover = discover
        self._factory = factory or (lambda board: PicoPwmHid(lambda: open_device(
            serial=board["serial_number"], path=None if board["serial_number"] else board["device_path"])))
        self._serial = serial
        self._path = path
        self._known: dict[str, BoardInfo] = {}
        self._clients: dict[str, PicoPwmHid] = {}
        self._lock = threading.RLock()

    def boards(self) -> BoardScan:
        with self._lock:
            boards: list[BoardInfo] = []
            seen = set()
            for device in self._discover():
                serial = device.get("serial_number") or None
                path = os.fsdecode(device["path"])
                identifier = hashlib.sha256((f"serial:{serial}" if serial else f"path:{path}")
                                            .encode(errors="surrogateescape")).hexdigest()
                if identifier in seen:
                    raise DeviceError("duplicate PicoPWM board identity; board serials must be unique")
                seen.add(identifier)
                boards.append({"id": identifier, "product_name": device.get("product_string") or "PicoPWM",
                               "serial_number": serial, "device_path": path})
            self._known.update((board["id"], board) for board in boards)
            preferred = next((board["id"] for board in boards
                              if (self._serial and board["serial_number"] == self._serial)
                              or (self._path and board["device_path"] == self._path)), None)
            return {"boards": boards, "default_board_id": preferred or (
                None if self._serial or self._path else boards[0]["id"] if boards else None)}

    def client(self, identifier: str | None) -> PicoPwmHid:
        with self._lock:
            if not identifier or identifier not in self._known:
                raise DeviceError("unknown board; rescan and select a listed board")
            if identifier not in self._clients:
                self._clients[identifier] = self._factory(self._known[identifier])
            return self._clients[identifier]

    def close(self) -> None:
        with self._lock:
            for client in self._clients.values():
                client.close()
            self._clients.clear()
