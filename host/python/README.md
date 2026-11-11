# PicoPWM

**Control and monitor 24 PWM channels from Python or a local web dashboard.**
Supports Python **3.10+** on Windows, Linux, and macOS.

[PyPI](https://pypi.org/project/picopwm/) ·
[Source](https://github.com/djwinter29-oss/PicoPWM) ·
[Firmware](https://github.com/djwinter29-oss/PicoPWM/releases) ·
[Report an issue](https://github.com/djwinter29-oss/PicoPWM/issues)

The package communicates over USB HID with a Raspberry Pi Pico or Pico 2 running PicoPWM firmware.
The included dashboard runs locally; your browser connects to Python, which communicates with the board.

## Requirements

- Python 3.10+, a USB data cable, and compatible
  [PicoPWM firmware](https://github.com/djwinter29-oss/PicoPWM/releases).
- OS permission to access the board. Linux users should follow the
  [HID permissions guide](https://github.com/djwinter29-oss/PicoPWM/blob/main/docs/guides/setup-and-troubleshooting.md#linux-hid-access).

## Quick Start

Create and activate a virtual environment:

**Linux/macOS**

```sh
python3 -m venv .venv
. .venv/bin/activate
```

**Windows PowerShell**

```powershell
py -m venv .venv
.\.venv\Scripts\Activate.ps1
```

If activation is blocked, use the
[activation-free instructions](https://github.com/djwinter29-oss/PicoPWM/blob/main/docs/guides/python-integration.md#windows-installation-without-activation).

Install and launch:

```sh
python -m pip install picopwm
picopwm
```

Connect the board to the computer running Python and open <http://127.0.0.1:5000/>.
Select the board; use **Rescan** if needed. Stop the service with `Ctrl+C`.
The older `picopwm-web` command remains an alias.
Routine Werkzeug request logs are suppressed; warnings and errors remain visible.

**Before driving outputs:** GPIO signals are 3.3 V only. Banks default to monitor-only.
Configure the intended bank as a generator and reboot before setting output.
Follow the [wiring guide](https://github.com/djwinter29-oss/PicoPWM/blob/main/docs/guides/hardware-setup.md).

## Use from Python

Read status without changing outputs:

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

For multiple boards, replace the client initialization, retaining the `try/finally` block:

```python
from picopwm.transport import open_device

client = PicoPwmHid(lambda: open_device(serial="YOUR_BOARD_SERIAL"))
```

After checking roles and wiring, use `client.set_channel(0, 1000, 50)` inside the `try` block to request
1 kHz / 50% duty, or `client.stop()` to stop all generators. Read back realized state; it is not an independent
electrical measurement. `status()` returns connection errors in its result; commands can raise `DeviceError`
on rejection or an unconfirmed outcome. Inspect state before retrying an unconfirmed command.

## Dashboard Configuration

Set these before starting `picopwm`; direct Python clients select boards through `open_device()` instead.

| Variable | Purpose | Default |
| --- | --- | --- |
| `PICOPWM_SERIAL` | Preferred board serial | Automatic selection |
| `PICOPWM_DEVICE_PATH` | Preferred HID path | Automatic selection |
| `PICOPWM_HOST` | Listen address | `127.0.0.1` |
| `PICOPWM_PORT` | Listen port | `5000` |
| `PICOPWM_TOKEN` | API access token | Unset |

Keep loopback binding for local use. Remote binding requires a token; the built-in server has no TLS.
Do not expose it directly to the internet. See the
[integration guide](https://github.com/djwinter29-oss/PicoPWM/blob/main/docs/guides/python-integration.md)
for authentication, HTTP APIs, and development.

## Troubleshooting

Compatibility depends on HID report layouts and advertised capabilities, not a minimum firmware version.
Unsupported identities or layouts produce protocol errors; optional controls require supporting firmware.
No minimum hardware-verified firmware release has been established.

- **Board missing or inaccessible:** check the cable, firmware, HID permissions, and board selection.
  Scripts with multiple boards must select a serial number or device path.
- **Control unavailable:** check the running bank role and firmware capabilities; monitors cannot be driven.
- **Readings:** temperature is an uncalibrated internal estimate; service load is not CPU utilization.

See [setup and troubleshooting](https://github.com/djwinter29-oss/PicoPWM/blob/main/docs/guides/setup-and-troubleshooting.md).
For bug reports, include package/firmware versions, OS, and the exact error message.

## License

[BSD 3-Clause](https://github.com/djwinter29-oss/PicoPWM/blob/main/LICENSE).
