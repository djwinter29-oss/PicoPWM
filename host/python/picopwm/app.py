"""Flask management interface for PicoPWM."""

from __future__ import annotations

import os
from pathlib import Path
from typing import Callable

from flask import Flask, jsonify, request, send_from_directory

from .backends import CdcTransport, I2cTransport
from .client import PicoPWMClient


def create_app(client: PicoPWMClient | None = None, client_factory: Callable[[], PicoPWMClient] | None = None) -> Flask:
    web_root = Path(__file__).resolve().parents[2] / "web"
    app = Flask(__name__, static_folder=web_root, static_url_path="")
    state = {"client": client, "client_factory": client_factory or build_client}

    def get_client() -> PicoPWMClient:
        if state["client"] is None:
            state["client"] = state["client_factory"]()
        return state["client"]

    @app.get("/")
    def index():
        return send_from_directory(app.static_folder, "index.html")

    @app.get("/api/channels")
    def channel_api():
        try:
            return jsonify([channel.as_dict() for channel in get_client().channels()])
        except Exception as exc:
            return jsonify({"error": str(exc)}), 503

    @app.post("/api/channels/<int:channel>")
    def update_channel(channel: int):
        payload = request.get_json(silent=True)
        if not isinstance(payload, dict):
            return jsonify({"error": "JSON object required"}), 400

        frequency = payload.get("frequencyHz")
        duty = payload.get("dutyPercent")
        if type(frequency) is not int or not 0 <= frequency <= 0xFFFFFFFF:
            return jsonify({"error": "frequencyHz must be an unsigned 32-bit integer"}), 400
        if type(duty) is not int or not 0 <= duty <= 100:
            return jsonify({"error": "dutyPercent must be between 0 and 100"}), 400

        try:
            updated = get_client().set_channel(channel, frequency, duty)
            return jsonify(updated.as_dict())
        except ValueError as exc:
            return jsonify({"error": str(exc)}), 400
        except Exception as exc:
            return jsonify({"error": str(exc)}), 503

    @app.post("/api/stop")
    def stop_all():
        try:
            get_client().stop_all()
            return "", 200
        except Exception as exc:
            return jsonify({"error": str(exc)}), 503

    return app


def build_client() -> PicoPWMClient:
    transport = os.environ.get("PICOPWM_TRANSPORT", "cdc").lower()
    if transport == "cdc":
        port = os.environ.get("PICOPWM_CDC_PORT", "/dev/ttyACM0")
        return PicoPWMClient(CdcTransport(port))
    if transport == "i2c":
        address = int(os.environ.get("PICOPWM_I2C_ADDRESS", "0x40"), 0)
        bus = int(os.environ.get("PICOPWM_I2C_BUS", "1"), 0)
        return PicoPWMClient(I2cTransport(address=address, bus=bus))
    raise ValueError("PICOPWM_TRANSPORT must be 'cdc' or 'i2c'")


def main() -> None:
    app = create_app()
    app.run(host=os.environ.get("PICOPWM_HOST", "127.0.0.1"), port=int(os.environ.get("PICOPWM_PORT", "5000")))


if __name__ == "__main__":
    main()
