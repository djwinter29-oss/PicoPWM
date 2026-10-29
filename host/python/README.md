# PicoPWM

**Control and monitor PicoPWM boards from Python or a local web dashboard.**

Designed for **Windows, Linux, and macOS** with Python **3.10+**.

[PyPI](https://pypi.org/project/picopwm/) ·
[Source](https://github.com/djwinter29-oss/PicoPWM) ·
[Firmware releases](https://github.com/djwinter29-oss/PicoPWM/releases) ·
[Report an issue](https://github.com/djwinter29-oss/PicoPWM/issues)

Script board operations with a Python HID client or use the included Flask
dashboard to inspect channels, adjust generator outputs, and save board settings.
Connect a Raspberry Pi Pico or Pico 2 running PicoPWM firmware; the firmware
provides 24 logical channels across three generator or monitor banks.

- **Python scripting:** read realized channel state and issue board commands
  without implementing the USB protocol.
- **Local dashboard:** inspect identity, pins, and telemetry, edit generator
  settings, and use confirmed **Save and Reboot**, when supported by firmware.
- **Multiple boards:** select by product and serial number, and reconnect to the
  selected board rather than silently switching to another.

This package communicates over **USB HID**, not the CDC serial shell or I2C.
The browser talks to Python, so it does not need WebHID support.

![PicoPWM dashboard showing device information, bank configuration, telemetry, and channel controls](https://raw.githubusercontent.com/djwinter29-oss/PicoPWM/main/docs/assets/pico-pwm-dashboard.png)

*Illustrative WebHID screenshot with simulated data, not a live hardware result.
The Python dashboard shares this layout; labels and columns may differ.*

## Quick start

Create and activate a virtual environment. On Linux or macOS:

```sh
python3 -m venv .venv
. .venv/bin/activate
```

On Windows PowerShell:

```powershell
py -m venv .venv
.\.venv\Scripts\Activate.ps1
```

If PowerShell blocks activation, use the
[activation-free installation](https://github.com/djwinter29-oss/PicoPWM/blob/main/docs/guides/python-integration.md#windows-installation-without-activation).

Then install and start PicoPWM:

```sh
python -m pip install picopwm
picopwm
```

The previous `picopwm-web` command remains available as a compatibility alias.

Connect your board and open **<http://127.0.0.1:5000>**. Choose the board in the
selector; use **Rescan** if it is not listed. Stop the application with `Ctrl+C`.
The board must be connected to the computer running Python, not necessarily the
computer displaying the browser.

> **Before driving outputs:** GPIO signals are 3.3 V only. The default firmware
> configuration is monitor-only. Configure the intended bank as a generator and
> reboot before setting its output. Follow the
> [hardware setup guide](https://github.com/djwinter29-oss/PicoPWM/blob/main/docs/guides/hardware-setup.md)
> and verify your wiring first.

## Requirements

- Python **3.10 or later** on Windows, Linux, or macOS.
- A Raspberry Pi Pico or Pico 2 flashed with compatible
  [PicoPWM firmware](https://github.com/djwinter29-oss/PicoPWM/releases).
- A USB data cable and permission to access the board's HID interface.

On Linux, follow the
[HID permissions guide](https://github.com/djwinter29-oss/PicoPWM/blob/main/docs/guides/setup-and-troubleshooting.md#linux-hid-access).
Older firmware may expose fewer features; unavailable dashboard actions are disabled.

Compatibility is checked using the
[HID report layouts](https://github.com/djwinter29-oss/PicoPWM/blob/main/docs/design/usb/hid-report-reference.md),
not a minimum firmware release number. The client expects layout version `1`,
PicoPWM product ID `0x0003`, and 24 channels; optional features are capability-gated.
Unsupported layouts or identities are reported as protocol errors. A minimum
hardware-verified firmware release has not yet been established.

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

If multiple boards are connected, replace the `client = PicoPwmHid()` line above
with the following, keeping the same `try/finally` block:

```python
from picopwm.transport import open_device

client = PicoPwmHid(lambda: open_device(serial="YOUR_BOARD_SERIAL"))
```

`status()` reports connection failures in its result. Mutating operations can
raise `DeviceError` when a command is rejected or its outcome is unconfirmed.
To change outputs, place commands inside the preceding `try` block, after the
connectivity check and before `finally` closes the client. After verifying bank
roles and wiring, `client.set_channel(0, 1000, 50)` requests 1 kHz at 50% duty,
and `client.stop()` stops all generator outputs. Read back realized state:
backend limits and timing quantization can affect the result.

Direct clients select boards through `open_device()` arguments; the environment
variables below select boards for the dashboard.

## Configuration

Set environment variables before starting `picopwm`:

| Variable | Purpose | Default |
| --- | --- | --- |
| `PICOPWM_SERIAL` | Preferred board serial number | Automatic selection |
| `PICOPWM_DEVICE_PATH` | Preferred HID device path | Automatic selection |
| `PICOPWM_HOST` | Listen address | `127.0.0.1` |
| `PICOPWM_PORT` | Listen port | `5000` |
| `PICOPWM_TOKEN` | API access token | Unset |

For another port, use `PICOPWM_PORT=5002 picopwm` on Linux/macOS, or set
`$env:PICOPWM_PORT = "5002"` before running `picopwm` in PowerShell.

Keep loopback binding for local use. Listening beyond loopback requires a token;
the built-in server has no TLS and should not be exposed directly to the internet.
See the [Python integration guide](https://github.com/djwinter29-oss/PicoPWM/blob/main/docs/guides/python-integration.md)
for authentication, network access, and the dashboard's JSON HTTP API.

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
for more help. Internal temperature is an estimate, not calibrated ambient
temperature; service load measures service-work wall time, not CPU utilization.

## Documentation and development

- [Python HTTP API and development guide](https://github.com/djwinter29-oss/PicoPWM/blob/main/docs/guides/python-integration.md)
- [HID protocol reference](https://github.com/djwinter29-oss/PicoPWM/blob/main/docs/design/usb/hid-report-reference.md)

From a repository checkout:

```sh
cd host/python
python -m pip install -e '.[test]'
python -m pytest -q
```

Alternatively, run `tools/host/run-python.sh` from the repository root to create a
virtual environment and start the dashboard.

## License

PicoPWM is released under the
[BSD 3-Clause License](https://github.com/djwinter29-oss/PicoPWM/blob/main/LICENSE).
For [bug reports](https://github.com/djwinter29-oss/PicoPWM/issues), include the
package and firmware versions, operating system, and exact error message.
