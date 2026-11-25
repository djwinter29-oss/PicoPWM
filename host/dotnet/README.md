# PicoPWM (.NET)

**Control and monitor 24 PWM channels through a local web dashboard.**
The ASP.NET Core application communicates with PicoPWM boards over USB HID; your browser connects to the
.NET service rather than directly to USB.

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

## Standalone Application

Release archives support **Linux x64, Windows x64, and macOS ARM64** and include the .NET runtime.
Current distribution is through GitHub Releases, not NuGet.

1. Download and extract `picopwm-host-v<VERSION>-<RID>` for your platform.
2. Connect your board to the computer running the application with a USB data cable.
3. Run `./PicoPwm` on Linux/macOS or `.\PicoPwm.exe` in Windows PowerShell.
4. Open <http://127.0.0.1:5000/>, select your board, and use **Rescan** if needed.

Each browser page keeps its own board selection; an absent board is not silently replaced.
Stop the service with `Ctrl+C`.
Routine ASP.NET request logs are suppressed; startup messages, warnings, and errors remain visible.
The dashboard displays the assembly informational version, including the version supplied by release builds.

The shared dashboard has bounded HTTP waits and fresh-status recovery after
uncertain writes; it never automatically replays commands. See
[Host App Development](../../docs/development/host-apps.md) for test coverage and limitations.

Before driving outputs, check the bank role and wiring in the
[hardware setup guide](https://github.com/djwinter29-oss/PicoPWM/blob/main/docs/guides/hardware-setup.md);
GPIOs are 3.3 V only.

## Shared Dashboard Controls

The Python and .NET dashboards share the [controls and readings](https://github.com/djwinter29-oss/PicoPWM/blob/main/host/direct/README.md#controls-and-readings)
with Direct Dashboard. Its browser connection instructions apply only to
Direct Dashboard.

## Configuration and Authentication

Set environment variables before starting the application:

| Variable | Purpose | Default |
| --- | --- | --- |
| `PICOPWM_SERIAL` / `PICOPWM_DEVICE_PATH` | Preferred board | Automatic selection |
| `PICOPWM_HOST` | Listen address | `127.0.0.1` |
| `PICOPWM_PORT` | Listen port | `5000` |
| `PICOPWM_TOKEN` | API access token | Unset |

`--urls` takes precedence over `ASPNETCORE_URLS`, then the host/port settings above.
The dashboard listens on loopback by default. Binding another address requires
`PICOPWM_TOKEN`, and the page then prompts for it. The server has no TLS: do not
expose it to untrusted networks, and put an HTTPS reverse proxy in front for
remote access.

## Development

For build, test, launcher, and packaging instructions, see
[Host App Development](../../docs/development/host-apps.md) and
[CI and Releases](../../docs/development/ci-and-releases.md). See
[Native HID Qualification](../../docs/validation/native-hid-qualification.md)
for platform-specific device checks.

## Help and License

See [troubleshooting](https://github.com/djwinter29-oss/PicoPWM/blob/main/docs/guides/troubleshooting.md).
Include application/firmware versions, OS, and the exact error in bug reports.

[BSD 3-Clause](https://github.com/djwinter29-oss/PicoPWM/blob/main/LICENSE).
