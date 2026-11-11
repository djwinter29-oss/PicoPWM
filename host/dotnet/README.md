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
  [HID permissions guide](https://github.com/djwinter29-oss/PicoPWM/blob/main/docs/guides/setup-and-troubleshooting.md#linux-hid-access).

## Standalone Application

Release archives support **Linux x64, Windows x64, and macOS ARM64** and include the .NET runtime.
Current distribution is through GitHub Releases, not NuGet.

1. Download and extract `picopwm-host-v<VERSION>-<RID>` for your platform.
2. Connect your board to the computer running the application with a USB data cable.
3. Run `./PicoPwm` on Linux/macOS or `.\PicoPwm.exe` in Windows PowerShell.
4. Open <http://127.0.0.1:5000/>, select your board, and use **Rescan** if needed.

Each browser page keeps its own board selection; an absent board is not silently replaced.
Stop the service with `Ctrl+C`.

**Before driving outputs:** GPIO signals are 3.3 V only. Banks default to monitor-only.
Configure the intended bank as a generator and reboot before setting output.
Follow the [wiring guide](https://github.com/djwinter29-oss/PicoPWM/blob/main/docs/guides/hardware-setup.md).

## Dashboard

- Click a generator's frequency or duty to edit it. **Apply changes live output immediately**;
  the 1 kHz / 50% preset only fills fields. Monitors are read-only.
- Edit startup settings through **Boot configuration**, then review and confirm **Save & Reboot**.
  Reconnect and verify settings afterward. Failed operations may leave partial target changes; inspect before retrying.
- Pin assignments come from the connected board and use GPIO IDs, not physical header-pin numbers.
  Unsupported firmware features show disabled controls or update guidance.
- Readings are not independent electrical measurements. Temperature is an internal estimate, RAM is static
  reservation, and service load is not CPU utilization.

See the [dashboard control guide](https://github.com/djwinter29-oss/PicoPWM/blob/main/host/direct/README.md#controls-and-readings)
for shared controls and safety notes; browser pairing applies only to Direct Dashboard.

## Configuration and Authentication

Set environment variables before starting the application:

| Variable | Purpose | Default |
| --- | --- | --- |
| `PICOPWM_SERIAL` / `PICOPWM_DEVICE_PATH` | Preferred board | Automatic selection |
| `PICOPWM_HOST` | Listen address | `127.0.0.1` |
| `PICOPWM_PORT` | Listen port | `5000` |
| `PICOPWM_TOKEN` | API access token | Unset |

`--urls` takes precedence over `ASPNETCORE_URLS`, then the host/port settings above.
Keep loopback binding for local use. Listening beyond loopback requires a token; do not expose the service
directly to the internet.

When a token is configured, the dashboard prompts for it. API clients use a Bearer authorization header
or `POST /api/login` with `{"token": "..."}`. The resulting `picopwm_dotnet_auth` cookie is separate from
Python's `picopwm_python_auth`; the old shared cookie is no longer accepted, so existing sessions must log in again.
Cross-origin requests are rejected; without a token, non-loopback request hosts are also rejected.

The HTTP API uses the same snake_case route and payload conventions as the
[Python host API](https://github.com/djwinter29-oss/PicoPWM/blob/main/docs/guides/python-integration.md).
Actions require a board ID from `GET /api/boards`. An unconfirmed command returns an error, not success.

## Development

Install the **.NET 10 SDK**. From the repository root, run:

```sh
dotnet run --project host/dotnet/PicoPwm
```

In another terminal:

```sh
dotnet test host/dotnet/PicoPwm.sln --configuration Release --nologo
```

The POSIX launcher `tools/host/run-dotnet.sh` also restores, builds, and starts the application.
With a newer SDK, install the .NET 10 ASP.NET Core runtime. Explicit major-version roll-forward is an
unverified compatibility option, not a substitute for testing against the targeted runtime.
Edit canonical assets in `host/web/src/` and `host/direct/src/`, not generated `wwwroot` output.

## Help and License

See [setup and troubleshooting](https://github.com/djwinter29-oss/PicoPWM/blob/main/docs/guides/setup-and-troubleshooting.md).
Include application/firmware versions, OS, and the exact error in bug reports.

[BSD 3-Clause](https://github.com/djwinter29-oss/PicoPWM/blob/main/LICENSE).
