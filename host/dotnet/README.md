# PicoPWM .NET host

This ASP.NET Core application serves the PicoPWM dashboard and controls the board over
the USB HID vendor interface (VID `0xCAFE`, PID `0x4012`) described in
[hid-report-reference.md](../../docs/design/usb/hid-report-reference.md). The firmware
implements it in `firmware/src/usb`.

## Requirements

- .NET 10 SDK, or a newer SDK with the .NET 10 ASP.NET Core runtime
- HID access to the board. On Linux, configure it with the
  [HID permissions guide](../../docs/guides/control/README.md#linux-hid-permissions).

From the repository root, `tools/host/run-dotnet.sh` runs the host. Set `PICOPWM_SERIAL`
or `PICOPWM_DEVICE_PATH` to preselect a board when desired.
The launcher restores NuGet dependencies and builds before starting. Set
`PICOPWM_PORT` / `PICOPWM_HOST` (default `127.0.0.1:5000`), `ASPNETCORE_URLS`, or
pass `--urls` to choose another listening address. `dotnet run` without those
settings listens on `http://127.0.0.1:5000`. A non-loopback URL is refused unless
`PICOPWM_TOKEN` is set. With that token, `/api/*` requires
`Authorization: Bearer <token>` or the `picopwm_auth` cookie from
`POST /api/login` with `{"token": "..."}`. The page itself stays open so the
browser can prompt once. A request whose `Origin` names a different host is
rejected. Without a token, a request whose `Host` is not loopback is rejected as well,
including a page reached by DNS rebinding. Keep board controls on loopback when the token is unset.
If only a newer runtime is installed, explicitly opt in to major-version
roll-forward with `DOTNET_ROLL_FORWARD=Major` or install the .NET 10 ASP.NET Core
runtime instead.

```sh
PICOPWM_PORT=5002 tools/host/run-dotnet.sh
```

```sh
cd host/dotnet
dotnet run --project PicoPwm
```

Open <http://127.0.0.1:5000>. The dashboard matches WebHID: identity and RAM, boot
configuration with confirmed **Save and Reboot**, compact pin assignment, uptime,
temperature and per-core service load, LED/reboot controls, and three eight-channel
tables. Generator frequency/duty values open an editing dialog; monitors are read-only.
Unsupported firmware features are disabled with update guidance.
Pin assignment, monitoring GPIOs and dialog GPIOs use the connected board's
capability-gated HID pin report, never a hard-coded host map. Unavailable pins show
`--`; older firmware shows update guidance and invalid reports show an error.

The PicoUart-style board selector scans boards attached to this server and offers
**Rescan**. Each page keeps its own selection. Reconnects stay pinned to the selected
board's serial (or path if no serial); an absent board is marked **not found** after
rescanning and never replaced silently by another.

Host HTML/HTTP code lives in `host/web/`; rendering, display constants and PWM styling
reuse `host/webhid/js/view.mjs`, `js/device.mjs`, and `css/webhid.css` directly. This
project links both sets into `wwwroot` during build/publish. No browser WebHID is
required. Edit canonical source files, not generated output.

The API is the same as the Python host (snake_case, as in the HID report layouts):

- `GET /api/boards` lists board IDs, products, serials, paths and `default_board_id`
- `GET /api/host` identifies the .NET dashboard
- `GET /api/status?board={id}` returns identity, configuration, runtime, `pins`
  (`channels`, `i2c_sda`, `i2c_scl`, `led`), `pins_error`, and all 24 channels, or
  `connected: false` with an `error`
- All actions require `?board={id}` from the scan. Status without a selection does
  not open a device; missing/unknown selections are rejected for commands.
- `POST /api/channels/{channel}?board={id}` with `{"freq_hz": 1000, "duty": 50}`
- `POST /api/led` with `{"on": true}`; `POST /api/reboot` with `{}`
- `POST /api/save-reboot` with `{"profile": 0, "address": 64, "roles": [0,1,1],
  "acknowledge_overclock": false}` validates the complete draft before writing,
  stops on the first failure, and confirms one save/reboot command.
  Already submitted targets are not rolled back on failure.
- Reboot actions return `{"rebooting": true}` after confirmation.
- `POST /api/stop`, `/api/bank`, `/api/i2c-address`, `/api/clock-profile`, `/api/save`

Errors return 400 for invalid input and 409 when the device rejects a command or its
outcome is unconfirmed; discovery/transport availability errors return 503.
Temperature is an internal die estimate; service load is not CPU utilization.

Build and test the complete solution with:

```sh
dotnet build PicoPwm.sln
dotnet test PicoPwm.sln
```
