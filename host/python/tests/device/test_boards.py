# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026 Ji Dong

from picopwm.device.boards import BoardRegistry
from ..fakes import modern


def test_registry_keeps_clients_pinned_across_reconnect_and_closes_them():
    discovered = [{"path": b"/dev/a", "serial_number": "A", "product_string": "PicoPWM"},
                  {"path": b"/dev/b", "serial_number": "B", "product_string": "PicoPWM"}]
    devices = {}

    def factory(board):
        devices[board["serial_number"]] = modern()
        return devices[board["serial_number"]][1]

    registry = BoardRegistry(discover=lambda: discovered, factory=factory, serial="B")
    scan = registry.boards()
    a, b = [board["id"] for board in scan["boards"]]
    assert scan["default_board_id"] == b
    assert registry.client(a).status()["connected"]
    assert registry.client(b).status()["connected"]
    discovered[0]["path"] = b"/dev/new"
    assert registry.boards()["boards"][0]["id"] == a
    assert registry.client(a) is devices["A"][1]
    discovered[:] = []
    assert not registry.boards()["boards"]
    assert registry.client(a) is devices["A"][1]
    registry.close()
    assert devices["A"][0].closed
    assert devices["B"][0].closed
