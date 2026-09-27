"""Flask management interface for PicoPWM."""

from __future__ import annotations

import os
from typing import Callable

from flask import Flask, flash, jsonify, redirect, render_template, request, url_for

from .backends import CdcTransport, I2cTransport
from .client import PicoPWMClient


def create_app(client: PicoPWMClient | None = None, client_factory: Callable[[], PicoPWMClient] | None = None) -> Flask:
    app = Flask(__name__)
    app.config["SECRET_KEY"] = os.environ.get("PICOPWM_SECRET_KEY", "picopwm-local")
    state = {"client": client, "client_factory": client_factory or build_client}

    def get_client() -> PicoPWMClient:
        if state["client"] is None:
            state["client"] = state["client_factory"]()
        return state["client"]

    @app.get("/")
    def index():
        try:
            channels = get_client().channels()
            return render_template("index.html", channels=channels, transport=transport_name())
        except Exception as exc:
            return render_template("index.html", channels=[], transport=transport_name(), error=str(exc)), 503

    @app.post("/channels/<int:channel>")
    def update_channel(channel: int):
        try:
            frequency = int(request.form["frequency_hz"])
            duty = int(request.form["duty_percent"])
            get_client().set_channel(channel, frequency, duty)
            flash(f"Channel {channel} updated", "success")
        except (KeyError, ValueError) as exc:
            flash(str(exc) or "Enter a valid frequency and duty", "error")
        except Exception as exc:
            flash(f"Channel {channel}: {exc}", "error")
        return redirect(url_for("index"))

    @app.post("/stop")
    def stop_all():
        try:
            get_client().stop_all()
            flash("All channels stopped", "success")
        except Exception as exc:
            flash(f"Stop failed: {exc}", "error")
        return redirect(url_for("index"))

    @app.get("/api/channels")
    def channel_api():
        try:
            return jsonify([channel.as_dict() for channel in get_client().channels()])
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


def transport_name() -> str:
    return os.environ.get("PICOPWM_TRANSPORT", "cdc").upper()


def main() -> None:
    app = create_app()
    app.run(host=os.environ.get("PICOPWM_HOST", "127.0.0.1"), port=int(os.environ.get("PICOPWM_PORT", "5000")))


if __name__ == "__main__":
    main()
