# PicoPWM (.NET)

Local dashboard for 24-channel PicoPWM boards. The browser connects to the
.NET service, which talks to the board over USB HID.

[Downloads](https://github.com/djwinter29-oss/PicoPWM/releases) ·
[Source](https://github.com/djwinter29-oss/PicoPWM) ·
[Firmware](https://github.com/djwinter29-oss/PicoPWM/releases) ·
[Report an issue](https://github.com/djwinter29-oss/PicoPWM/issues)

## Requirements

- A Raspberry Pi Pico or Pico 2 running compatible
  [PicoPWM firmware](https://github.com/djwinter29-oss/PicoPWM/releases), connected to the service's computer
  by a USB data cable.
- OS access to the HID interface. Linux users should follow the
  [HID permissions guide](https://github.com/djwinter29-oss/PicoPWM/blob/main/docs/guides/linux-hid-access.md).

Firmware compatibility follows HID report layouts and advertised capabilities,
not a minimum version. No minimum hardware-verified firmware release has been
established; unsupported identities or layouts produce protocol errors.

## Standalone Application

Release archives for **Linux x64, Windows x64, and macOS ARM64** include the
.NET runtime. Download them from GitHub Releases, not NuGet.

1. Download and extract `picopwm-host-v<VERSION>-<RID>` for your platform.
2. Connect your board to the computer running the application with a USB data cable.
3. Run `./PicoPwm` on Linux/macOS or `.\PicoPwm.exe` in Windows PowerShell.
4. Open <http://127.0.0.1:5000/>, select your board, and use **Rescan** if needed.

Each page keeps its own board selection; missing boards are not replaced.
`Ctrl+C` stops the service, **not generator outputs**. Use the
[stop procedure](../../docs/guides/dashboard-controls.md#stop-an-output)
before leaving the board.

Before driving outputs, check the bank role and wiring in the
[hardware setup guide](https://github.com/djwinter29-oss/PicoPWM/blob/main/docs/guides/hardware-setup.md);
GPIOs are 3.3 V only.

## Dashboard Controls

See [channel editing, readings, and saving](../../docs/guides/dashboard-controls.md).

## Configuration and Authentication

Set environment variables before starting the application:

| Variable | Purpose | Default |
| --- | --- | --- |
| `PICOPWM_SERIAL` / `PICOPWM_DEVICE_PATH` | Preferred board | Automatic selection |
| `PICOPWM_HOST` | Listen address | `127.0.0.1` |
| `PICOPWM_PORT` | Listen port | `5000` |
| `PICOPWM_TOKEN` | API access token | Unset |

`--urls` takes precedence over `ASPNETCORE_URLS`, then the host/port settings above.
Non-loopback binding requires `PICOPWM_TOKEN`; the page prompts for it.
There is no TLS. Keep the server off untrusted networks; use an HTTPS reverse
proxy for remote access.

## Development

[Builds and tests](../../docs/development/host-apps.md#net-host) ·
[Packaging](../../docs/development/ci-and-releases.md) ·
[Native HID checks](../../docs/validation/native-hid-qualification.md).

## Help and License

See [troubleshooting](https://github.com/djwinter29-oss/PicoPWM/blob/main/docs/guides/troubleshooting.md).
After an unconfirmed write, inspect fresh status before retrying; commands are
not replayed automatically. Include dashboard application/firmware versions,
OS, and the exact error in bug reports.

[BSD 3-Clause](https://github.com/djwinter29-oss/PicoPWM/blob/main/LICENSE).
