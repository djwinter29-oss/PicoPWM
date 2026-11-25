# Dashboard Controls

These controls apply to Direct, Python, and .NET dashboards connected to a board.

## Set an Output

1. Check the channel's GPIO in **Pin assignment**. Use 3.3 V-safe wiring and
   common ground; never connect two generator outputs together.
2. Check the running bank role. Channels 0-7 belong to Bank A (hardware),
   8-15 to B (PIO), and 16-23 to C (software).
3. If the bank is a monitor, select Generator in **Boot configuration**,
   confirm **Save & Reboot**, reconnect, and verify the running role.
4. Edit the channel's underlined frequency or duty and click **Apply**.
   This changes live output immediately. Read back the realized values.

Start at 1 kHz / 50% duty.

## Stop an Output

The dashboards have no Stop button. Closing the page, disconnecting, or
stopping the host service **leaves generator outputs active**.

Click **Disconnect** in Direct, or stop the Python/.NET service with `Ctrl+C`.
Open its USB CDC port in a serial terminal: `/dev/ttyACM*` on Linux, `COMx`
on Windows, or `/dev/cu.usbmodem*` on macOS. Run:

```text
pwm stop all
pwm get all
```

Verify every generator reports `0 Hz / 0%`. If stop fails or the state is
uncertain, disconnect board power. Stop preserves saved bank roles.
Close the terminal before reconnecting the dashboard.

## Controls and Readings

| Control | Behavior |
| --- | --- |
| PWM tables | A: channels 0-7, B: 8-15, C: 16-23. Roles apply to whole banks; monitors are read-only. |
| Channel editor | Opens with realized values. Unit conversion preserves frequency. The preset fills fields only; Cancel writes nothing. |
| Boot configuration | Clock, decimal I2C address (8-119), and roles. Edits stay local until confirmed. |
| Pin assignment | GPIO numbers, not header-pin numbers. |
| Board LED / Reboot Board | Toggle the LED or restart without saving edits. |

**Unsaved edits** means local changes. **Target differs from running** is not
proof of persistence. Read errors are shown explicitly; unsupported controls
need supporting firmware.

Generator readback describes configured timing, not an instrument measurement.
RAM is reserved memory; temperature is an uncalibrated internal estimate;
service load is not CPU utilization.
See [operating ranges](configuration.md#pwm-operating-ranges) and the
[HID telemetry definitions](../protocols/usb-hid.md#report-id-3-firmware-memory).

## Saving Safely

- Stop and verify generators through CDC before saving, then reconnect the
  dashboard. Flash writes pause channel service and communication.
- Explicit profiles require acknowledgement: 150 MHz overclocks RP2040;
  200/250 MHz overclock both MCUs and are not qualified for every board.
  Defaults are 125 MHz on RP2040 and 150 MHz on RP2350. Keep the
  [recovery procedure](troubleshooting.md#recover-an-unbootable-configuration-or-reset-flash)
  available.
- Failed settings commands prevent saving but can leave partial targets.
  After failure or an unconfirmed result, reconnect and inspect targets
  before retrying.

To connect a dashboard, use the [browser](../../host/direct/README.md),
[Python](../../host/python/README.md), or [.NET](../../host/dotnet/README.md) guide.
