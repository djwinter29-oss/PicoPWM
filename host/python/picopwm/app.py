"""Flask management interface for PicoPWM, backed by the HID vendor interface."""

from __future__ import annotations

import os
from pathlib import Path

from flask import Flask, jsonify, request, send_from_directory

from .client import DeviceError, PicoPwmHid
from .transport import open_device


def _int(payload: dict, name: str) -> int:
    value = payload.get(name)
    if type(value) is not int:
        raise ValueError(f"{name} must be an integer")
    return value


def create_app(client: PicoPwmHid | None = None) -> Flask:
    # Wheels bundle the dashboard as picopwm/web; source checkouts use host/web.
    web_root = Path(__file__).resolve().parent / "web"
    if not web_root.is_dir():
        web_root = Path(__file__).resolve().parents[2] / "web"
    app = Flask(__name__, static_folder=web_root, static_url_path="")
    hid = client or PicoPwmHid(lambda: open_device(os.environ.get("PICOPWM_SERIAL"),
                                                   os.environ.get("PICOPWM_DEVICE_PATH")))

    @app.get("/")
    def index():
        return send_from_directory(app.static_folder, "index.html")

    @app.get("/api/status")
    def status():
        return jsonify(hid.status())

    def action(call):
        payload = request.get_json(silent=True)
        if not isinstance(payload, dict):
            return jsonify({"error": "JSON object required"}), 400
        try:
            return jsonify(call(payload))
        except ValueError as error:
            return jsonify({"error": str(error)}), 400
        except DeviceError as error:
            return jsonify({"error": str(error)}), 409

    @app.post("/api/channels/<int:channel>")
    def set_channel(channel: int):
        return action(lambda p: hid.set_channel(channel, _int(p, "freq_hz"), _int(p, "duty")))

    @app.post("/api/stop")
    def stop():
        return action(lambda p: hid.stop())

    @app.post("/api/bank")
    def bank():
        return action(lambda p: hid.set_bank(_int(p, "bank"), _int(p, "backend"), _int(p, "role")))

    @app.post("/api/i2c-address")
    def i2c_address():
        return action(lambda p: hid.set_i2c_address(_int(p, "address")))

    @app.post("/api/clock-profile")
    def clock_profile():
        return action(lambda p: hid.set_clock_profile(_int(p, "profile"), p.get("acknowledge_overclock") is True))

    @app.post("/api/save")
    def save():
        return action(lambda p: hid.save_configuration(p.get("acknowledge_overclock") is True))

    return app


def main() -> None:
    app = create_app()
    app.run(host=os.environ.get("PICOPWM_HOST", "127.0.0.1"), port=int(os.environ.get("PICOPWM_PORT", "5000")))


if __name__ == "__main__":
    main()
