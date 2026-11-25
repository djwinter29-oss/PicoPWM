# PicoPWM

Python client and local dashboard for 24-channel PicoPWM boards.
Runs on Windows, Linux, and macOS.

[PyPI](https://pypi.org/project/picopwm/) ·
[Source](https://github.com/djwinter29-oss/PicoPWM) ·
[Firmware](https://github.com/djwinter29-oss/PicoPWM/releases) ·
[Report an issue](https://github.com/djwinter29-oss/PicoPWM/issues)

Python talks USB HID to the connected Pico or Pico 2; the browser talks to Python.

## Requirements

- Python 3.10+, a USB data cable, and compatible
  [PicoPWM firmware](https://github.com/djwinter29-oss/PicoPWM/releases).
- OS permission to access the board. Linux users should follow the
  [HID permissions guide](https://github.com/djwinter29-oss/PicoPWM/blob/main/docs/guides/linux-hid-access.md).

Firmware compatibility follows HID report layouts and advertised capabilities,
not a minimum version. No minimum hardware-verified firmware release has been
established; unsupported identities or layouts produce protocol errors.

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

If PowerShell blocks activation, use the environment's executables directly:

```powershell
.\.venv\Scripts\python.exe -m pip install picopwm
.\.venv\Scripts\picopwm.exe
```

Install and launch:

```sh
python -m pip install picopwm
picopwm
```

Connect the board, open <http://127.0.0.1:5000/>, and select it; use **Rescan**
if needed. `Ctrl+C` stops the service, **not generator outputs**. Use the
[stop procedure](https://github.com/djwinter29-oss/PicoPWM/blob/main/docs/guides/dashboard-controls.md#stop-an-output)
before leaving the board.

Before driving outputs, check running roles and the
[hardware setup guide](https://github.com/djwinter29-oss/PicoPWM/blob/main/docs/guides/hardware-setup.md);
GPIOs are 3.3 V only. The
[dashboard guide](https://github.com/djwinter29-oss/PicoPWM/blob/main/docs/guides/dashboard-controls.md)
covers channel editing and saving.

## Use from Python

Read status without changing outputs:

```python
from picopwm.device.client import PicoPwmHid

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

`with PicoPwmHid() as client:` also closes handles automatically, but does
**not** stop outputs. After checking roles and wiring,
call `client.set_channel(0, 1000, 50)` to request 1 kHz / 50% duty, or
`client.stop()` to stop all generators. Read back realized state.

`status()` returns connection errors; rejected or unconfirmed commands raise
`DeviceError`. Inspect state before retrying. A stalled USB transfer can delay
other calls, including stop; disconnect power if output state is uncertain.
See
[Native HID Qualification](https://github.com/djwinter29-oss/PicoPWM/blob/main/docs/validation/native-hid-qualification.md)
for platform-specific transfer checks.

## Dashboard Configuration

Set these before starting `picopwm`; direct Python clients select boards through `open_device()` instead.

| Variable | Purpose | Default |
| --- | --- | --- |
| `PICOPWM_SERIAL` | Preferred board serial | Automatic selection |
| `PICOPWM_DEVICE_PATH` | Preferred HID path | Automatic selection |
| `PICOPWM_HOST` | Listen address | `127.0.0.1` |
| `PICOPWM_PORT` | Listen port | `5000` |
| `PICOPWM_TOKEN` | API access token | Unset |

Non-loopback binding requires `PICOPWM_TOKEN`; the page prompts for it.
There is no TLS. Keep the server off untrusted networks; use an HTTPS reverse
proxy for remote access.

## Troubleshooting

- **Board missing or inaccessible:** check the cable, firmware, HID permissions, and board selection.
  Scripts with multiple boards must select a serial number or device path.
- **Control unavailable:** check the running bank role and firmware capabilities; monitors cannot be driven.
- **Readings:** temperature is an uncalibrated internal estimate; service load is not CPU utilization.

The old `picopwm-web` command remains an alias.
For other problems, see
[troubleshooting](https://github.com/djwinter29-oss/PicoPWM/blob/main/docs/guides/troubleshooting.md).
Include package/firmware versions, OS, and the exact error in bug reports.

## Development

[Build and test instructions](https://github.com/djwinter29-oss/PicoPWM/blob/main/docs/development/host-apps.md#python-host).

## License

[BSD 3-Clause](https://github.com/djwinter29-oss/PicoPWM/blob/main/LICENSE).
