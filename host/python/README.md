# PicoPWM

**Control and monitor PicoPWM boards from Python or a local web dashboard.**

[PyPI](https://pypi.org/project/picopwm/) ·
[Source](https://github.com/djwinter29-oss/PicoPWM) ·
[Firmware releases](https://github.com/djwinter29-oss/PicoPWM/releases) ·
[Report an issue](https://github.com/djwinter29-oss/PicoPWM/issues)

PicoPWM provides a USB HID client and a Flask dashboard for Raspberry Pi Pico and
Pico 2 boards running PicoPWM firmware. Read the state of 24 PWM channels, adjust
generator outputs, and manage board configuration without writing USB protocol code.

## Features

- **24 logical channels:** three fixed eight-channel banks, each configured as
  generators or monitors.
- **Interactive dashboard:** view frequency and duty, edit generator outputs, and
  inspect board identity, pin assignments, and runtime telemetry.
- **Board management:** identification LED, reboot, and confirmed configuration
  saving with **Save and Reboot**, when supported by the firmware.
- **Multiple boards:** select a board by product and serial number; reconnect to
  the selected board rather than silently switching to another.
- **Python integration:** use the HID client directly in scripts or provide a
  client to the Flask application factory.

This package communicates over **USB HID**, not the firmware's CDC serial shell
or I2C interface. The browser talks to the local Python application, so it does
not need WebHID support.

## Requirements

- Python **3.10 or later** on Windows, Linux, or macOS.
- A Raspberry Pi Pico or Pico 2 flashed with compatible
  [PicoPWM firmware](https://github.com/djwinter29-oss/PicoPWM/releases).
- A USB data cable and permission to access the board's HID interface.

On Linux, follow the
[HID permissions guide](https://github.com/djwinter29-oss/PicoPWM/blob/main/docs/guides/setup-and-troubleshooting.md#linux-hid-access).
Older firmware may expose fewer features; unavailable dashboard actions are disabled.

## Quick start

Install the package, preferably in a virtual environment:

```sh
python -m pip install picopwm
picopwm-web
```

Connect your board and open **<http://127.0.0.1:5000>**. Choose the board in the
selector; use **Rescan** if it is not listed. Stop the application with `Ctrl+C`.

The board must be connected to the computer running Python, not necessarily the
computer displaying the browser.

> **Before driving outputs:** GPIO signals are 3.3 V only. The default firmware
> configuration is monitor-only. Configure the intended bank as a generator and
> reboot before setting its output. Follow the
> [hardware setup guide](https://github.com/djwinter29-oss/PicoPWM/blob/main/docs/guides/hardware-setup.md)
> and verify your wiring first.

## Configuration

Configure the application with environment variables before starting `picopwm-web`:

| Variable | Purpose | Default |
| --- | --- | --- |
| `PICOPWM_SERIAL` | Preferred board serial number | Automatic selection |
| `PICOPWM_DEVICE_PATH` | Preferred HID device path | Automatic selection |
| `PICOPWM_HOST` | Listen address | `127.0.0.1` |
| `PICOPWM_PORT` | Listen port | `5000` |
| `PICOPWM_TOKEN` | Token required for API access when configured | Unset |

For example, use another port on Linux or macOS:

```sh
PICOPWM_PORT=5002 picopwm-web
```

In Windows PowerShell:

```powershell
$env:PICOPWM_PORT = "5002"
picopwm-web
```

### Network access

Keep the default loopback address for local use. Listening beyond loopback requires
`PICOPWM_TOKEN`; the application refuses a non-loopback bind without it.

When a token is configured, the dashboard prompts for it. API clients can send
`Authorization: Bearer <token>` or authenticate through `POST /api/login` with
`{"token": "<token>"}` to receive a login cookie. Requests with a different
`Origin` than the request host are rejected.

The built-in server does not provide TLS. Do not expose it directly to the public
internet; use a trusted network and an appropriately configured HTTPS reverse
proxy if remote access is needed.

## Use from Python

Read board and channel state without changing outputs:

```python
from picopwm.client import PicoPwmHid

client = PicoPwmHid()
try:
    state = client.status()
    if not state["connected"]:
        raise RuntimeError(state["error"])
    print(state["identity"])
    for channel in state["channels"]:
        print(channel)
finally:
    client.close()
```

If multiple boards are connected, select one explicitly:

```python
from picopwm.client import PicoPwmHid
from picopwm.transport import open_device

client = PicoPwmHid(lambda: open_device(serial="YOUR_BOARD_SERIAL"))
```

Close the client when finished. `status()` reports connection failures in its
result. Mutating operations can raise `DeviceError` when a command is rejected
or its outcome cannot be confirmed.

For generator channels, `client.set_channel(channel=0, freq_hz=1000, duty=50)`
requests 1 kHz at 50% duty, and `client.stop()` stops all generator outputs.
Only send these commands after confirming the bank role and fixture wiring.
Read back realized channel state: backend limits and timing quantization can
make it differ from the request.

Applications and tests can inject a client with `create_app(client)` from
`picopwm.app`. Direct HID clients use the supplied `open_device()` arguments for
board selection; the environment variables above select boards for the dashboard.

## HTTP API

The dashboard exposes a JSON API using snake_case fields:

| Endpoint | Purpose |
| --- | --- |
| `GET /api/boards` | Discover boards and obtain board IDs |
| `GET /api/host` | Identify the dashboard implementation |
| `GET /api/status?board=<id>` | Read identity, configuration, telemetry, pins, and channels |
| `POST /api/channels/<channel>?board=<id>` | Set frequency and duty using `{"freq_hz": 1000, "duty": 50}` |
| `POST /api/stop?board=<id>` | Stop generator outputs |
| `POST /api/led?board=<id>` | Set the identification LED using `{"on": true}` |
| `POST /api/reboot?board=<id>` | Reboot the board |
| `POST /api/save-reboot?board=<id>` | Validate, save, and reboot with target configuration |

Use board IDs returned by discovery. Board actions require an explicit selection;
unknown or missing selections are rejected. Status without a selection is
disconnected and does not open a HID device.

The save-and-reboot payload contains `profile`, `address`, `roles`, and
`acknowledge_overclock`, for example
`{"profile": 0, "address": 64, "roles": [0, 1, 1], "acknowledge_overclock": false}`.
Already submitted configuration edits are not rolled back if a later step fails.
Legacy configuration endpoints `/api/bank`, `/api/i2c-address`, `/api/clock-profile`,
and `/api/save` remain available with a board selection.

Invalid input returns HTTP 400; rejected or unconfirmed device commands return
409; board discovery or transport availability failures return 503. Reboot success
is reported only after command confirmation.

## Troubleshooting

- **No board found:** check the USB data cable, firmware, HID permissions, and board
  selection. The vendor interface uses VID `0xCAFE` and PID `0x4012`.
- **Multiple boards found in a script:** pass a serial number or device path to
  `open_device()`.
- **A control is unavailable:** check the bank's running role and the firmware's
  advertised capabilities. Monitor channels cannot be driven.
- **A command outcome is unconfirmed:** reconnect and inspect realized state before
  retrying, particularly after configuration saves or reboot.

See the
[setup and troubleshooting guide](https://github.com/djwinter29-oss/PicoPWM/blob/main/docs/guides/setup-and-troubleshooting.md)
and [HID protocol reference](https://github.com/djwinter29-oss/PicoPWM/blob/main/docs/design/usb/hid-report-reference.md)
for details. Internal temperature is an estimate, not calibrated ambient
temperature; service load measures service-work wall time, not CPU utilization.

## Development

From a repository checkout:

```sh
cd host/python
python -m pip install -e '.[test]'
python -m pytest -q
```

Alternatively, run `tools/host/run-python.sh` from the repository root to create a
virtual environment, refresh the editable installation, and start the dashboard.
The launcher uses `https://pypi.org/simple` by default; set `PIP_INDEX_URL` to use
another index. If `ensurepip` is unavailable, it can use the base interpreter's
pip with `--python`; otherwise install pip or the OS Python venv package.

Release wheels bundle the dashboard assets. Source checkouts serve the shared
assets from `host/web/` and `host/webhid/`; edit those canonical files rather than
generated or bundled copies.

## License

PicoPWM is released under the
[BSD 3-Clause License](https://github.com/djwinter29-oss/PicoPWM/blob/main/LICENSE).
For bug reports, include the package and firmware versions, operating system, and
the exact error message in a
[GitHub issue](https://github.com/djwinter29-oss/PicoPWM/issues).
