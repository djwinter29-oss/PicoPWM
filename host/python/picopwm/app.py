"""Flask management interface for PicoPWM, backed by the HID vendor interface."""

from __future__ import annotations

import os
from pathlib import Path

from flask import Flask, jsonify, request, send_from_directory

from .client import DeviceError, PicoPwmHid
from .boards import BoardRegistry
from .protocol import ProtocolError


def _int(payload: dict, name: str) -> int:
    value = payload.get(name)
    if type(value) is not int:
        raise ValueError(f"{name} must be an integer")
    return value


def create_app(client: PicoPwmHid | None = None, registry: BoardRegistry | None = None) -> Flask:
    # Wheels bundle the dashboard as picopwm/web; source checkouts use host/web.
    web_root = Path(__file__).resolve().parent / "web"
    if not (web_root / "index.html").is_file():
        web_root = Path(__file__).resolve().parents[2] / "web"
    app = Flask(__name__, static_folder=web_root, static_url_path="")
    boards = registry or BoardRegistry(serial=os.environ.get("PICOPWM_SERIAL"),
                                      path=os.environ.get("PICOPWM_DEVICE_PATH"))
    app.extensions["picopwm_boards"] = boards

    def selected():
        return client if client is not None else boards.client(request.args.get("board"))

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
            return jsonify(selected().status())
        except DeviceError as error:
            return jsonify({"error": str(error)}), 409

    @app.get("/api/boards")
    def scan():
        try:
            return jsonify(boards.boards())
        except (OSError, RuntimeError) as error:
            return jsonify({"error": str(error)}), 503

    @app.get("/api/host")
    def host():
        return jsonify({"dashboard_type": "Python"})

    def action(call):
        payload = request.get_json(silent=True)
        if not isinstance(payload, dict):
            return jsonify({"error": "JSON object required"}), 400
        try:
            return jsonify(call(selected(), payload))
        except ProtocolError as error:
            return jsonify({"error": str(error)}), 409
        except ValueError as error:
            return jsonify({"error": str(error)}), 400
        except DeviceError as error:
            return jsonify({"error": str(error)}), 409
        except (OSError, RuntimeError) as error:
            return jsonify({"error": str(error)}), 503

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
    app = create_app()
    try:
        app.run(host=os.environ.get("PICOPWM_HOST", "127.0.0.1"), port=int(os.environ.get("PICOPWM_PORT", "5000")))
    finally:
        app.extensions["picopwm_boards"].close()


if __name__ == "__main__":
    main()
