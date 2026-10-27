# PicoPWM .NET host

This ASP.NET Core application serves the PicoPWM dashboard and controls the board over
the USB HID vendor interface (VID `0xCAFE`, PID `0x4012`) described in
[hid-report-reference.md](../../docs/design/usb/hid-report-reference.md). The firmware
implements it in `firmware/src/usb`; this build ships with the clock-profile capability cleared.

## Requirements

- .NET 8 SDK
- HID access to the board (on Linux, a udev rule or access to `/dev/hidraw*`)

From the repository root, `tools/host/run-dotnet.sh` runs the host. Set `PICOPWM_SERIAL`
or `PICOPWM_DEVICE_PATH` when more than one board is attached.

```sh
cd host/dotnet
dotnet run --project PicoPwm
```

Open <http://localhost:5000>. The dashboard mirrors the WebHID page: identity, bank
configuration, channel controls, clock profile, and save.

The dashboard's canonical HTML, CSS, and JavaScript live in `host/web/`; this project
links those files into `wwwroot` during build and publish. Make frontend changes in the
shared source, not in generated or linked output.

The API is the same as the Python host (snake_case, as in the HID report layouts):

- `GET /api/status` returns identity, configuration, and all 24 channels, or
  `connected: false` with an `error`
- `POST /api/channels/{channel}` with `{"freq_hz": 1000, "duty": 50}`
- `POST /api/stop`, `/api/bank`, `/api/i2c-address`, `/api/clock-profile`, `/api/save`

Errors return 400 for invalid input and 409 when the device rejects a command or its
outcome is unconfirmed.

Build and test the complete solution with:

```sh
dotnet build PicoPwm.sln
dotnet test PicoPwm.sln
```
