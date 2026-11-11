# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026 Ji Dong

import logging
from unittest.mock import Mock

import pytest

from picopwm.dashboard.app import create_app
from picopwm.device.client import DeviceError, PicoPwmHid
from picopwm.hid import protocol as p
from ..fakes import make, modern


def test_server_suppresses_request_logs_but_keeps_warnings_and_errors(monkeypatch, caplog):
    from picopwm.dashboard import app as app_module

    caplog.set_level(logging.INFO, logger="werkzeug")
    monkeypatch.setenv("PICOPWM_HOST", "127.0.0.1")
    monkeypatch.setenv("PICOPWM_PORT", "5000")
    boards = Mock()
    app = Mock(extensions={"picopwm_boards": boards})

    def run(**kwargs):
        logger = logging.getLogger("werkzeug")
        logger.info("routine request")
        logger.warning("server warning")
        logger.error("server error")

    app.run.side_effect = run
    monkeypatch.setattr(app_module, "create_app", lambda **kwargs: app)
    app_module.main()
    assert "routine request" not in caplog.text
    assert "server warning" in caplog.text
    assert "server error" in caplog.text
    app.run.assert_called_once_with(host="127.0.0.1", port=5000)
    boards.close.assert_called_once()



def test_status_without_device():
    def opener():
        raise RuntimeError("PicoPWM HID interface not found")

    state = PicoPwmHid(opener).status()
    assert not state["connected"] and "not found" in state["error"]
    response = create_app(PicoPwmHid(opener)).test_client().get("/api/status")
    assert response.json["error"] == "Device status unavailable"



def test_api():
    _, client = make()
    http = create_app(client).test_client()
    assert http.get("/api/status").json["connected"]
    assert http.post("/api/channels/2", json={"freq_hz": 500, "duty": 10}).status_code == 200
    invalid_duty = http.post("/api/channels/2", json={"freq_hz": 500, "duty": 101})
    assert invalid_duty.status_code == 400
    assert invalid_duty.json == {"error": "Invalid request fields"}
    assert http.post("/api/channels/2", json={"freq_hz": "x", "duty": 1}).status_code == 400
    assert http.post("/api/stop", json={}).status_code == 200
    _, dropping = make("drop")
    assert create_app(dropping).test_client().post("/api/stop", json={}).status_code == 409



def test_api_does_not_expose_device_exception_details():
    private_detail = "private device path /secret/device"

    class FailedClient:
        def status(self):
            return {"connected": False, "error": private_detail, "identity_error": private_detail,
                    "configuration_error": None, "runtime_error": None, "pins_error": None,
                    "bank_errors": [private_detail, None, None]}

        def stop(self):
            raise OSError(private_detail)

    http = create_app(FailedClient()).test_client()
    status = http.get("/api/status")
    assert status.json["error"] == "Device status unavailable"
    assert status.json["identity_error"] == "Device status unavailable"
    assert status.json["bank_errors"] == ["Device status unavailable", None, None]
    assert private_detail not in status.get_data(as_text=True)

    action = http.post("/api/stop", json={})
    assert action.status_code == 503
    assert action.json == {"error": "Device operation failed"}
    assert private_detail not in action.get_data(as_text=True)



@pytest.mark.parametrize("route", ["/api/status", "/api/stop"])
def test_api_redacts_all_returned_status_errors(route):
    private_detail = "private device path /secret/device"

    class FailedClient:
        def status(self):
            return {"connected": True, "error": private_detail, "identity_error": private_detail,
                    "configuration_error": private_detail, "runtime_error": private_detail,
                    "pins_error": private_detail, "bank_errors": [private_detail, None, private_detail],
                    "channels": [{"channel": 0, "freq_hz": 1000, "duty": 50}]}

        stop = status

    http = create_app(FailedClient()).test_client()
    response = http.get(route) if route == "/api/status" else http.post(route, json={})
    assert response.status_code == 200
    for field in ("error", "identity_error", "configuration_error", "runtime_error", "pins_error"):
        assert response.json[field] == "Device status unavailable"
    assert response.json["bank_errors"] == ["Device status unavailable", None, "Device status unavailable"]
    assert response.json["connected"]
    assert response.json["channels"] == [{"channel": 0, "freq_hz": 1000, "duty": 50}]
    assert private_detail not in response.get_data(as_text=True)



@pytest.mark.parametrize("report_id", [
    p.CONFIGURATION_REPORT, p.RUNTIME_REPORT, p.PINS_REPORT, p.BANK_STATUS_REPORT,
])
@pytest.mark.parametrize("failure_type", [p.ProtocolError, OSError, RuntimeError, None])
def test_api_action_redacts_readback_errors(monkeypatch, report_id, failure_type):
    private_detail = "private readback detail /secret/device"
    device, client = make()
    identity = bytearray(device.identity)
    identity[16] |= 64 | 128
    device.identity = bytes(identity)
    read = device.get_feature_report

    def failed_readback(requested_id, length):
        if device.sent and requested_id == report_id:
            if failure_type is None:
                return bytes([requested_id])
            raise failure_type(private_detail)
        return read(requested_id, length)

    monkeypatch.setattr(device, "get_feature_report", failed_readback)
    response = create_app(client).test_client().post("/api/stop", json={})
    assert device.sent[0][3] == p.OP_STOP
    assert response.status_code == 200
    assert private_detail not in response.get_data(as_text=True)
    if failure_type in (p.ProtocolError, None):
        field = {p.CONFIGURATION_REPORT: "configuration_error", p.RUNTIME_REPORT: "runtime_error",
                 p.PINS_REPORT: "pins_error", p.BANK_STATUS_REPORT: "bank_errors"}[report_id]
        error = response.json[field]
        assert (error[0] if field == "bank_errors" else error) == "Device status unavailable"
        assert response.json["connected"]
    else:
        assert response.json["error"] == "Device status unavailable"
        assert not response.json["connected"]



@pytest.mark.parametrize(("failure", "status_code", "message"), [
    (p.ProtocolError("private protocol detail"), 409, "Invalid response from device"),
    (DeviceError("private device detail"), 409, "Device request failed"),
    (OSError("private operating system detail"), 503, "Device operation failed"),
    (RuntimeError("private runtime detail"), 503, "Device operation failed"),
])
def test_api_action_exceptions_are_not_returned(failure, status_code, message):
    class FailedClient:
        def stop(self):
            raise failure

    response = create_app(FailedClient()).test_client().post("/api/stop", json={})
    assert response.status_code == status_code
    assert response.json == {"error": message}
    assert str(failure) not in response.get_data(as_text=True)



@pytest.mark.parametrize(("failure", "status_code", "message"), [
    (p.ProtocolError("private protocol detail"), 409, "Invalid response from device"),
    (DeviceError("private device detail"), 409, "Device request failed"),
    (OSError("private operating system detail"), 503, "Device operation failed"),
    (RuntimeError("private runtime detail"), 503, "Device operation failed"),
    (Exception("private internal state"), 500, "Internal server error"),
])
def test_api_status_exceptions_are_not_returned(failure, status_code, message):
    class FailedClient:
        def status(self):
            raise failure

    response = create_app(FailedClient()).test_client().get("/api/status?board=1")
    assert response.status_code == status_code
    assert response.json == {"error": message}
    assert str(failure) not in response.get_data(as_text=True)



def test_api_action_unexpected_exception_returns_generic_error():
    private_detail = "private internal state"

    class FailedClient:
        def stop(self):
            raise Exception(private_detail)

    response = create_app(FailedClient()).test_client().post("/api/stop", json={})
    assert response.status_code == 500
    assert response.json == {"error": "Internal server error"}
    assert private_detail not in response.get_data(as_text=True)



@pytest.mark.parametrize("failure", [
    OSError("private operating system detail"),
    RuntimeError("private runtime detail"),
])
def test_board_discovery_exceptions_are_not_returned(failure):
    from picopwm.device.boards import BoardRegistry

    def discover():
        raise failure

    response = create_app(registry=BoardRegistry(discover=discover)).test_client().get("/api/boards")
    assert response.status_code == 503
    assert response.json == {"error": "Board discovery failed"}
    assert str(failure) not in response.get_data(as_text=True)



def test_status_exception_is_not_returned():
    private_detail = "private device path /secret/device"

    class FailedClient:
        def status(self):
            raise DeviceError(private_detail)

    response = create_app(FailedClient()).test_client().get("/api/status")
    assert response.status_code == 409
    assert response.json == {"error": "Device request failed"}
    assert private_detail not in response.get_data(as_text=True)



def test_pin_report_errors_are_explicit_and_disconnect_clears_the_map():
    device, client = modern()
    original = device.get_feature_report

    def invalid_pins(report_id, length):
        if report_id == p.PINS_REPORT:
            return bytes([p.PINS_REPORT, 1, 48, *range(23), 26, 27, 25])
        return original(report_id, length)

    device.get_feature_report = invalid_pins
    state = client.status()
    assert state["connected"] and state["pins"] is None
    assert "invalid pin" in state["pins_error"]
    response = create_app(client).test_client().get("/api/status")
    assert response.json["pins_error"] == "Device status unavailable"
    device.get_feature_report = original
    assert client.status()["pins"]["channels"][23] == 23

    def unplugged(report_id, length):
        raise OSError("device unplugged")

    device.get_feature_report = unplugged
    state = client.status()
    assert not state["connected"] and state["pins"] is None



def test_routes_keep_board_selection_pinned():
    from picopwm.device.boards import BoardRegistry

    discovered = [{"path": b"/dev/a", "serial_number": "A", "product_string": "PicoPWM"},
                  {"path": b"/dev/b", "serial_number": "B", "product_string": "PicoPWM"}]
    devices = {}

    def factory(board):
        devices[board["serial_number"]] = modern()
        return devices[board["serial_number"]][1]

    registry = BoardRegistry(discover=lambda: discovered, factory=factory, serial="B")
    http = create_app(registry=registry).test_client()
    scan = http.get("/api/boards").json
    a, b = [board["id"] for board in scan["boards"]]
    assert scan["default_board_id"] == b
    assert not http.get("/api/status").json["connected"]
    assert http.get("/api/status?board=unknown").status_code == 409
    assert http.get(f"/api/status?board={a}").json["runtime"]["temperature_c"] == 22.9
    assert http.post(f"/api/led?board={b}", json={"on": True}).status_code == 200
    assert devices["A"][0].sent == []
    assert devices["B"][0].sent[-1][3] == 8
    assert http.post(f"/api/led?board={b}", json={"on": 1}).status_code == 400
    save_reboot = http.post(f"/api/save-reboot?board={b}", json={
        "profile": 0, "address": 64, "roles": [0, 1, 1]})
    assert save_reboot.status_code == 200 and save_reboot.json == {"rebooting": True}
    assert http.post("/api/reboot", json={}).status_code == 409
    assert http.get("/api/host").json["dashboard_type"] == "Python"
    for path in ["/", "/css/direct.css", "/js/view.mjs", "/js/device.mjs", "/js/client.mjs"]:
        assert http.get(path).status_code == 200
    registry.close()



def test_scan_errors_are_generic_and_logged(caplog):
    from picopwm.device.boards import BoardRegistry

    registry = BoardRegistry(discover=lambda: [{"path": b"a", "serial_number": "same"},
                                              {"path": b"b", "serial_number": "same"}])
    response = create_app(registry=registry).test_client().get("/api/boards")
    assert response.status_code == 503
    assert response.json == {"error": "Board discovery failed"}
    assert "duplicate PicoPWM board identity" in caplog.text



def test_bind_and_dashboard_reject_remote_and_foreign_callers():
    from picopwm.dashboard.app import bind_host

    assert bind_host(None, None) == "127.0.0.1"
    assert bind_host("localhost", "") == "localhost"
    assert bind_host("0.0.0.0", "secret") == "0.0.0.0"
    with pytest.raises(RuntimeError, match="loopback"):
        bind_host("0.0.0.0", "")

    _, client = make()
    open_host = create_app(client).test_client()
    assert open_host.get("/api/status", headers={"Host": "evil.example"}).status_code == 403
    assert open_host.get("/api/status").status_code == 200
    http = create_app(client, token="secret").test_client()
    assert http.get("/").status_code == 200
    assert http.get("/api/status", headers={"Origin": "http://evil.example"}).status_code == 403
    assert http.get("/api/status", headers={"Authorization": "Bearer secret",
                                            "Origin": "http://evil.example"}).status_code == 403
    assert http.get("/api/status").status_code == 401
    assert http.post("/api/login", json={"token": "nope"}).status_code == 401
    login = http.post("/api/login", json={"token": "secret"})
    assert login.status_code == 200
    assert login.headers["Set-Cookie"].startswith("picopwm_python_auth=")
    assert "HttpOnly" in login.headers.get("Set-Cookie", "")
    assert http.get("/api/status").status_code == 200
    other = create_app(client, token="secret").test_client()
    assert other.get("/api/status", headers={"Authorization": "Bearer secret"}).status_code == 200
    assert create_app(client).test_client().post("/api/login", json={"token": "x"}).status_code == 404



def test_python_login_cookie_is_independent_of_dotnet_and_legacy_cookies():
    import hashlib

    http = create_app(token="secret").test_client()
    value = hashlib.sha256(b"secret").hexdigest()
    http.set_cookie("picopwm_dotnet_auth", value)
    http.set_cookie("picopwm_auth", value)
    assert http.get("/api/host").status_code == 401
    assert http.post("/api/login", json={"token": "secret"}).status_code == 200
    http.set_cookie("picopwm_dotnet_auth", "other")
    assert http.get("/api/host").status_code == 200
