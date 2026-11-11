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
Routine ASP.NET request logs are suppressed; startup messages, warnings, and errors remain visible.
The dashboard displays the assembly informational version, including the version supplied by release builds.

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
Required action fields must be present: omitted numeric fields are rejected, not treated as zero.
Overclock acknowledgement remains optional and defaults to false.

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

Source files under `PicoPwm/` are grouped by responsibility:

- `Dashboard/`: binding, authentication checks, and HTTP response handling.
- `Device/`: thread-safe device client and board registry.
- `Hid/`: USB discovery, feature-report transport, and protocol codecs.

`Program.cs` remains the application entry point. Tests mirror these folders under `PicoPwm.Tests/`,
with shared device doubles in `Fakes.cs`. Existing namespaces and CLI behavior are unchanged;
no compatibility wrappers or extra `src/` layer are needed.

Client operations serialize prerequisite validation, command execution, and readback reconciliation per board.
Returned status snapshots do not expose cached pin arrays. `DeviceException.Status` carries a firmware
status code when available, and wrapped failures retain their `InnerException`; HTTP error sentences remain fixed.
The registry owns its clients and is disposed by the service container. Standalone callers can use
`using var client = new PicoPwmHid(...);` to release handles. Dispose/Close do not stop generator outputs,
and subsequent operations may reopen the client. Cleanup I/O failures are logged when a logger is supplied;
the application supplies its logger automatically.

Polling deadlines and configured HID read/write timeouts are not proof that native feature-report calls
cannot stall on every backend. Cross-platform disconnect, reconnect, and stall qualification remains a
hardware task; follow the [failure qualification checklist](../../docs/guides/python-integration.md#hid-failure-qualification-checklist)
with the .NET runtime and HidSharp version recorded instead of Python/hidapi versions.
Do not treat the host as an emergency-stop mechanism.

### Automated transport and concurrency checks

Hardware-free tests exercise HidSharp device discovery and streams using in-memory subclasses:
VID/PID and usage filtering, serial/path selection, absent and ambiguous devices, permission failures,
feature-report IDs, platform-specific prefixes, buffer sizing, writes, timeouts configuration, and disposal.
Both Linux and non-Linux framing policies are tested on any test host; this does not execute those OS drivers.
The test seams are internal; the public discovery and transport APIs are unchanged.

Lock regressions check ownership and execute a separate thread's immediate lock attempt rather than
assuming that a 100 ms delay proves contention. Clock/save dispatch also checks that the outer logical
operation still owns the gate in addition to the inner command lock.

### Native HID qualification still required

| Platform | Disconnect/reconnect | Native transfer stall | Shutdown/handle cleanup |
| --- | --- | --- | --- |
| Windows | Not run | Not run | Not run |
| Linux | Not run | Not run | Not run |
| macOS | Not run | Not run | Not run |

These are outstanding qualification cases, not failed tests or automated passes. Before running them,
the operator must identify the board, flashed artifact, CDC port and fixture, and authorize output changes.
Record the OS, architecture, .NET and HidSharp versions, command, elapsed time, confirmed/unconfirmed outcome,
selected-board identity after reconnect, and cleanup result. Mark a stall that cannot be safely reproduced
as not tested. Never automatically save, reboot, change roles, or rewire to make qualification possible.
Keep results and instrument checks separate from the hardware-free suite.

## Help and License

See [setup and troubleshooting](https://github.com/djwinter29-oss/PicoPWM/blob/main/docs/guides/setup-and-troubleshooting.md).
Include application/firmware versions, OS, and the exact error in bug reports.

[BSD 3-Clause](https://github.com/djwinter29-oss/PicoPWM/blob/main/LICENSE).
