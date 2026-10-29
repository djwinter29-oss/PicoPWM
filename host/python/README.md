# PicoPWM host tools

This package provides a Python HID client and a Flask management interface for the
firmware's 24 logical channels over the USB HID vendor interface (VID `0xCAFE`, PID
`0x4012`; see [hid-report-reference.md](../../docs/design/usb/hid-report-reference.md)).
The firmware implements this interface in `firmware/src/usb`.

## Install

Install the published package with `python -m pip install picopwm`.

From `host/python`:

```sh
python -m pip install -e '.[test]'
```

The `hidapi` dependency needs HID access to the board. On Linux, configure it with the
[setup and troubleshooting guide](../../docs/guides/setup-and-troubleshooting.md#linux-hid-access).
From the repository root, `tools/host/run-python.sh` creates a
virtualenv, installs the host and runs it.
The launcher refreshes the editable installation and runtime dependencies on each
start, including dependency version constraints. If Python lacks `ensurepip`, it
can create a pip-free venv and use the base interpreter's pip (`--python`) to install
into it. Install pip or the OS Python venv package if neither interpreter has pip.
The launcher uses `https://pypi.org/simple` by default for these public dependencies;
set `PIP_INDEX_URL` to use another package index.

## Run the web interface

```sh
picopwm-web
```

Optionally set `PICOPWM_SERIAL` or `PICOPWM_DEVICE_PATH` to preselect a board, and
`PICOPWM_HOST` / `PICOPWM_PORT` to change the listen address (default `127.0.0.1:5000`).
A host other than `127.0.0.1`, `localhost`, or `::1` is refused unless `PICOPWM_TOKEN`
is set. With that token, `/api/*` requires `Authorization: Bearer <token>` or the
`picopwm_auth` cookie from `POST /api/login` with `{"token": "..."}`. Pages stay
open so the browser can prompt once. A request whose `Origin` names a different
host is rejected. Leave the token unset on loopback; the dashboard then accepts
local API calls with no extra header. Without a token, a request whose `Host` is not
`127.0.0.1`, `localhost`, or `::1` is rejected, including a page reached by DNS rebinding.

Open <http://127.0.0.1:5000>. The dashboard mirrors the WebHID page: device identity
and RAM, boot configuration with one confirmed **Save and Reboot**, compact pin
assignment, runtime telemetry, LED/reboot controls, and three eight-channel tables.
Click a generator frequency or duty to edit its live output; monitor values are
read-only. Frequency uses Hz, kHz, or MHz units, and the optional 1 kHz / 50% preset
only fills the dialog until Apply is clicked. Old firmware disables unsupported actions and reports missing telemetry.
Pin assignment, monitoring GPIOs and dialog GPIOs come from the connected board's
capability-gated HID pin report, not a host-side map. Missing pins show `--`;
older firmware shows update guidance and malformed reports show an error.

Like PicoUart, the page scans boards attached to the **host server**, offers a
product/serial selector and **Rescan**, and initially selects the preferred or first
board. Each browser page keeps its own selection. Unplugging or rebooting retries
that same board, never silently switches to another, and a rescan labels an absent
selected board as **not found**. Select another board explicitly to switch.

The Flask app factory accepts an injected `PicoPwmHid`, which keeps tests independent of
hardware:

```python
from picopwm.app import create_app
from picopwm.client import PicoPwmHid

app = create_app(PicoPwmHid(lambda: my_device))
```

## Shared Web UI

The host-specific HTML and HTTP transport are in `host/web/`. Rendering, PWM styles,
and display constants reuse `host/webhid/js/view.mjs`, `js/device.mjs`, and
`css/webhid.css` directly; no WebHID access is needed by these server dashboards.
Flask serves the canonical assets in source checkouts; release wheel workflows
bundle them alongside the host assets. The .NET project links the same files at
build/publish time. Do not edit generated or bundled copies.

Both hosts expose the same JSON API (snake_case, as in the HID report layouts):

- `GET /api/boards` returns `boards` (ID, product, serial, path) and `default_board_id`
- `GET /api/host` returns the dashboard type
- `GET /api/status?board={id}` returns identity, configuration, runtime, `pins`
  (`channels`, `i2c_sda`, `i2c_scl`, `led`), `pins_error`, and all 24 channels, or
  `connected: false` with an `error`
- Every action requires `?board={id}` from the board scan; unknown/missing selections
  are rejected. Status without a selection is disconnected, without opening HID.
- `POST /api/channels/{channel}?board={id}` with `{"freq_hz": 1000, "duty": 50}`
- `POST /api/led` with `{"on": true}`; `POST /api/reboot` with `{}`
- `POST /api/save-reboot` with `{"profile": 0, "address": 64, "roles": [0,1,1],
  "acknowledge_overclock": false}` validates all fields before sending targets.
  Failures stop the sequence; already submitted target edits are not rolled back.
  Reboot responses return `{"rebooting": true}` only after command confirmation.
- `POST /api/stop`, `/api/bank`, `/api/i2c-address`, `/api/clock-profile`, `/api/save`
  remain available for existing API clients, with a board selection.

Errors return 400 for invalid input and 409 when the device rejects a command or its
outcome is unconfirmed; board discovery/transport availability errors return 503.
Temperature is an internal die estimate, not calibrated ambient temperature.
Service load measures service-work wall time, not CPU utilization.

Run the focused tests from this directory with:

```sh
python -m pytest -q
```
