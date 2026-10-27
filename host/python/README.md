# PicoPWM host tools

This package provides a Python HID client and a Flask management interface for the
firmware's 24 logical channels over the USB HID vendor interface (VID `0xCAFE`, PID
`0x4012`; see [hid-report-reference.md](../../docs/design/usb/hid-report-reference.md)).
The firmware does not implement HID yet, so no device is found until it does.

## Install

From `host/python`:

```sh
python -m pip install -e '.[test]'
```

The `hidapi` dependency needs HID access to the board (on Linux, a udev rule or access
to `/dev/hidraw*`). From the repository root, `tools/host/run-python.sh` creates a
virtualenv, installs the host and runs it.

## Run the web interface

```sh
picopwm-web
```

Set `PICOPWM_SERIAL` or `PICOPWM_DEVICE_PATH` when more than one board is attached, and
`PICOPWM_HOST` / `PICOPWM_PORT` to change the listen address (default `127.0.0.1:5000`).

Open <http://127.0.0.1:5000>. The dashboard mirrors the WebHID page: identity, bank
configuration, channel controls, clock profile, and save.

The Flask app factory accepts an injected `PicoPwmHid`, which keeps tests independent of
hardware:

```python
from picopwm.app import create_app
from picopwm.client import PicoPwmHid

app = create_app(PicoPwmHid(lambda: my_device))
```

## Shared Web UI

The shared frontend source is in `host/web/`: the WebHID-style `index.html`, `css/`, and
`js/`. Flask serves these files from the repository checkout, and the .NET host links
them into its static web root. Keep UI changes in this source directory rather than
copying them into either host project. The documented editable install keeps this
shared directory available to the Python app.

Both hosts expose the same JSON API (snake_case, as in the HID report layouts):

- `GET /api/status` returns identity, configuration, and all 24 channels, or
  `connected: false` with an `error`
- `POST /api/channels/{channel}` with `{"freq_hz": 1000, "duty": 50}`
- `POST /api/stop`, `/api/bank`, `/api/i2c-address`, `/api/clock-profile`, `/api/save`

Errors return 400 for invalid input and 409 when the device rejects a command or its
outcome is unconfirmed.

Run the focused tests from this directory with:

```sh
python -m pytest -q
```
