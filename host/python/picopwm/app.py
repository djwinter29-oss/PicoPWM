# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026 Ji Dong

"""Flask management interface for PicoPWM, backed by the HID vendor interface."""

from __future__ import annotations

import hashlib
import hmac
import os
from pathlib import Path
from urllib.parse import urlparse

from flask import Flask, jsonify, request, send_from_directory

from .client import DeviceError, PicoPwmHid
from .boards import BoardRegistry
from .protocol import ProtocolError


def _int(payload: dict, name: str) -> int:
    value = payload.get(name)
    if type(value) is not int:
        raise ValueError(f"{name} must be an integer")
    return value


LOOPBACK_HOSTS = {"127.0.0.1", "localhost", "::1", "[::1]"}


def bind_host(host: str | None, token: str | None) -> str:
    """Return a listen host, refusing a non-loopback bind when no token is configured."""
    chosen = host or "127.0.0.1"
    if chosen not in LOOPBACK_HOSTS and not token:
        raise RuntimeError("Refusing to listen beyond loopback without PICOPWM_TOKEN.")
    return chosen


def _cookie_value(token: str) -> str:
    return hashlib.sha256(token.encode("utf-8")).hexdigest()


def _same_secret(presented: str, expected: str) -> bool:
    if len(presented) != len(expected):
        return False
    return hmac.compare_digest(presented, expected)


def _request_hostname(value: str) -> str:
    if value.startswith("["):
        end = value.find("]")
        return value[1:end] if end > 1 else value
    if ":" in value:
        return value.rsplit(":", 1)[0]
    return value


def _request_is_loopback(value: str) -> bool:
    return _request_hostname(value).lower() in LOOPBACK_HOSTS


def _origin_allowed(origin: str, request_host: str) -> bool:
    if not origin:
        return True
    parsed = urlparse(origin)
    return bool(parsed.netloc) and parsed.netloc.lower() == request_host.lower()


def _credential_accepted(token: str, authorization: str | None, cookie: str | None) -> bool:
    if cookie and _same_secret(cookie, _cookie_value(token)):
        return True
    prefix = "Bearer "
    if authorization and authorization.startswith(prefix):
        return _same_secret(authorization[len(prefix):], token)
    return False


def create_app(client: PicoPwmHid | None = None, registry: BoardRegistry | None = None,
               token: str | None = None) -> Flask:
    # Wheels bundle the dashboard as picopwm/web; source checkouts use host/web.
    web_root = Path(__file__).resolve().parent / "web"
    if not (web_root / "index.html").is_file():
        web_root = Path(__file__).resolve().parents[2] / "web"
    app = Flask(__name__, static_folder=web_root, static_url_path="")
    app.config["PICOPWM_TOKEN"] = token or ""
    boards = registry or BoardRegistry(serial=os.environ.get("PICOPWM_SERIAL"),
                                      path=os.environ.get("PICOPWM_DEVICE_PATH"))
    app.extensions["picopwm_boards"] = boards

    @app.before_request
    def guard_request():
        configured = app.config["PICOPWM_TOKEN"]
        if not configured and not _request_is_loopback(request.host):
            return jsonify({"error": "refusing a non-loopback host without PICOPWM_TOKEN"}), 403
        if not _origin_allowed(request.headers.get("Origin", ""), request.host):
            return jsonify({"error": "cross-origin request rejected"}), 403
        if not configured or request.path == "/api/login" or not request.path.startswith("/api"):
            return None
        if _credential_accepted(configured, request.headers.get("Authorization"),
                                request.cookies.get("picopwm_auth")):
            return None
        return jsonify({"error": "unauthorized"}), 401

    @app.post("/api/login")
    def login():
        configured = app.config["PICOPWM_TOKEN"]
        if not configured:
            return jsonify({"error": "authentication is disabled"}), 404
        payload = request.get_json(silent=True)
        presented = payload.get("token") if isinstance(payload, dict) else None
        if type(presented) is not str or not _same_secret(presented, configured):
            return jsonify({"error": "unauthorized"}), 401
        response = jsonify({"ok": True})
        response.set_cookie("picopwm_auth", _cookie_value(configured), httponly=True, samesite="Strict")
        return response

    def selected():
        return client if client is not None else boards.client(request.args.get("board"))

    def device_response(state):
        """Serialize a client result without copying exception text into the HTTP response."""
        if "rebooting" in state:
            return jsonify({"rebooting": state["rebooting"] is True})
        unavailable = "Device status unavailable"
        response = {"connected": state.get("connected"), "identity": state.get("identity"),
                    "configuration": state.get("configuration"), "runtime": state.get("runtime"),
                    "pins": state.get("pins"), "channels": state.get("channels")}
        for field in ("error", "identity_error", "configuration_error", "runtime_error", "pins_error"):
            failed = state.get(field) is not None
            if failed:
                app.logger.warning("Device status %s: %s", field, state[field])
            response[field] = unavailable if failed else None
        bank_errors = state.get("bank_errors")
        if bank_errors is not None:
            for bank, error in enumerate(bank_errors):
                if error is not None:
                    app.logger.warning("Device status bank %s: %s", bank, error)
            response["bank_errors"] = [unavailable if error is not None else None for error in bank_errors]
        else:
            response["bank_errors"] = None
        return jsonify(response)

    # Source checkouts and packaged wheels serve the same canonical WebHID presentation assets.
    shared_root = web_root if (web_root / "css/webhid.css").is_file() else web_root.parent / "webhid"
    for asset in ("css/webhid.css", "js/view.mjs", "js/device.mjs"):
        app.add_url_rule(f"/{asset}", endpoint=asset,
                         view_func=lambda asset=asset: send_from_directory(shared_root, asset))

    @app.get("/")
    def index():
        return send_from_directory(app.static_folder, "index.html")

    @app.get("/api/status")
    def status():
        if client is None and not request.args.get("board"):
            return jsonify({"connected": False, "identity": None, "configuration": None,
                            "runtime": None, "channels": [], "bank_errors": [None] * 3, "error": None})
        try:
            return device_response(selected().status())
        except ProtocolError as error:
            app.logger.warning("Device returned an invalid response: %s", error)
            return jsonify({"error": "Invalid response from device"}), 409
        except DeviceError as error:
            app.logger.warning("Status request failed: %s", error)
            return jsonify({"error": "Device request failed"}), 409
        except (OSError, RuntimeError) as error:
            app.logger.warning("Status request failed: %s", error)
            return jsonify({"error": "Device operation failed"}), 503
        except Exception:
            app.logger.exception("Unexpected status failure")
            return jsonify({"error": "Internal server error"}), 500

    @app.get("/api/boards")
    def scan():
        try:
            return jsonify(boards.boards())
        except (OSError, RuntimeError) as error:
            app.logger.warning("Board discovery failed: %s", error)
            return jsonify({"error": "Board discovery failed"}), 503

    @app.get("/api/host")
    def host():
        return jsonify({"dashboard_type": "Python"})

    def action(call):
        payload = request.get_json(silent=True)
        if not isinstance(payload, dict):
            return jsonify({"error": "JSON object required"}), 400
        try:
            return device_response(call(selected(), payload))
        except ProtocolError as error:
            app.logger.warning("Device returned an invalid response: %s", error)
            return jsonify({"error": "Invalid response from device"}), 409
        except ValueError:
            return jsonify({"error": "Invalid request fields"}), 400
        except DeviceError as error:
            app.logger.warning("Device request failed: %s", error)
            return jsonify({"error": "Device request failed"}), 409
        except (OSError, RuntimeError) as error:
            app.logger.warning("Device operation failed: %s", error)
            return jsonify({"error": "Device operation failed"}), 503
        except Exception:
            app.logger.exception("Unexpected device operation failure")
            return jsonify({"error": "Internal server error"}), 500

    @app.post("/api/channels/<int:channel>")
    def set_channel(channel: int):
        return action(lambda hid, p: hid.set_channel(channel, _int(p, "freq_hz"), _int(p, "duty")))

    @app.post("/api/stop")
    def stop():
        return action(lambda hid, p: hid.stop())

    @app.post("/api/bank")
    def bank():
        return action(lambda hid, p: hid.set_bank(_int(p, "bank"), _int(p, "backend"), _int(p, "role")))

    @app.post("/api/i2c-address")
    def i2c_address():
        return action(lambda hid, p: hid.set_i2c_address(_int(p, "address")))

    @app.post("/api/clock-profile")
    def clock_profile():
        return action(lambda hid, p: hid.set_clock_profile(_int(p, "profile"), p.get("acknowledge_overclock") is True))

    @app.post("/api/save")
    def save():
        return action(lambda hid, p: hid.save_configuration(p.get("acknowledge_overclock") is True))

    @app.post("/api/led")
    def led():
        def apply(hid, payload):
            if type(payload.get("on")) is not bool:
                raise ValueError("on must be a boolean")
            return hid.set_led(payload["on"])
        return action(apply)

    @app.post("/api/reboot")
    def reboot():
        return action(lambda hid, p: hid.reboot())

    @app.post("/api/save-reboot")
    def save_reboot():
        def apply(hid, payload):
            roles = payload.get("roles")
            if not isinstance(roles, list):
                raise ValueError("roles must be a list of three bank roles")
            return hid.save_and_reboot(_int(payload, "profile"), _int(payload, "address"), roles,
                                      payload.get("acknowledge_overclock") is True)
        return action(apply)

    return app


def main() -> None:
    token = os.environ.get("PICOPWM_TOKEN", "")
    host = bind_host(os.environ.get("PICOPWM_HOST"), token)
    app = create_app(token=token)
    try:
        app.run(host=host, port=int(os.environ.get("PICOPWM_PORT", "5000")))
    finally:
        app.extensions["picopwm_boards"].close()


if __name__ == "__main__":
    main()
