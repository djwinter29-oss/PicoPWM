# Setup and Troubleshooting

Use this guide to configure the board, resolve common connection issues, and
find the right next check when a command or output does not behave as expected.
For command syntax, see the [USB CDC shell guide](usb-cdc-shell.md). For
implementation and backend constraints, see [PWM Driver Configuration](../design/pwm-driver/configuration.md).

## Configure Bank Roles

Each bank has a fixed backend and logical channel range, and its role can be
selected as generator or monitor. See [Hardware Setup](hardware-setup.md) for
the authoritative channel-to-GPIO map.

| Bank | Backend | Logical channels |
| --- | --- | --- |
| A | Hardware PWM | `0..7` |
| B | PIO | `8..15` |
| C | Software | `16..23` |

The default roles are monitor-only, so the board does not drive PWM outputs
until a bank is configured as a generator. Role changes are applied at startup
and do not switch a bank while the firmware is running.

On the CDC shell, run `config get` to inspect running and target settings. To
change one or more settings, use `config set`, for example:

```text
config set banka=generator
config set bankb=monitor, clock=default, address=64
```

The complete request is validated before any setting is changed. A successful
CDC `config set` saves the target configuration and reboots to apply it. The
CDC command supports bank roles, clock profiles (`default`, `150`, `200`, or
`250` MHz), and the 7-bit I2C address (`8..119`). HID supports its own
configuration workflow; see the [HID Report
Reference](../design/usb/hid-report-reference.md). The documented I2C
interface is limited to PWM channel reads, writes, status, and stop-all; see the
[I2C Protocol Reference](../design/i2c/i2c-protocol.md).

The SDK default is 125 MHz on RP2040 and 150 MHz on RP2350. The `150` MHz
profile is an overclock only on RP2040. HID/dashboard saves require
acknowledgement above 150 MHz; CDC has no separate acknowledgement step.
The `200` and `250` MHz profiles are not qualified for every board. Configuration persistence uses
flash and temporarily stalls CDC, HID, and I2C; allow up to two seconds for the
operation. See [Hardware Setup](hardware-setup.md) before wiring signals.

## Linux HID Access

The Python and .NET host applications need operating-system access to the
PicoPWM HID device. Run these commands once from the Linux account that will
use the host:

```sh
tools/host/hid-permissions-setup.sh
tools/host/hid-permissions-check.sh
```

The setup helper uses `sudo` to install a udev rule for PicoPWM USB and
`hidraw` nodes. It grants access to the account's primary group and desktop
`uaccess`; it does not change group membership or overwrite a customized
rule. Reconnect the board after setup if it remains inaccessible.

WebHID also requires browser permission. Run the udev setup on the computer
physically connected to the board; permission on a remote server does not
grant the browser access to a local USB device.

## Troubleshooting

### The board does not appear in the terminal

- Confirm the board is powered and use a USB data cable, not a charge-only
  cable.
- Check that the board is running PicoPWM firmware and that the host opened
  its CDC serial device. Common device names are `/dev/ttyACM*` on Linux,
  `COMx` on Windows, and `/dev/cu.usbmodem*` on macOS.
- Close another terminal or program that may already have the serial port
  open. USB CDC does not depend on the terminal baud-rate setting.
- Reconnect the board and retry. If necessary, reflash the appropriate
  `pico` or `pico2` firmware from [Releases](https://github.com/djwinter29-oss/PicoPWM/releases).

### A Python or .NET host reports HID access denied

- On Linux, run the two commands in [Linux HID Access](#linux-hid-access),
  reconnect the board, and run the permission check again.
- Close another program that may have the HID device open.
- Confirm the board is connected to the same computer that runs the host
  application.

### WebHID cannot find or open the board

- Use a desktop browser that supports WebHID, such as Chrome or Edge.
- Connect the board directly to the computer running the browser, then grant
  the site access when prompted.
- On Linux, also configure OS access as described in
  [Linux HID Access](#linux-hid-access). Browser permission does not replace
  device-node permission.
- Close other HID clients and retry the browser connection.

### PWM output stays low or does not match the request

- Run `config get` and check the **running** role for the channel's bank. If
  it is a monitor, configure that bank as a generator and apply the change
  before requesting output.
- Check the channel-to-GPIO mapping in [Hardware Setup](hardware-setup.md)
  before connecting an instrument or load.
- Run `pwm get <channel>` and compare the realized frequency and duty with
  the request. A generator at `0 Hz / 0%` is intentionally held low; at
  `0 Hz / 100%` it is held high.
- Confirm signal grounds are shared and that connected signals are 3.3 V
  safe. Do not connect outputs together.
- Use a suitable oscilloscope or logic analyzer to verify the physical
  output. Requested frequency ranges are not accuracy guarantees; see the
  [backend design pages](../design/pwm-driver/).

### A configuration change does not take effect

- Compare the running and target values with `config get`. The running role
  changes only after reboot.
- CDC `config set` saves and reboots automatically. HID configuration changes
  have their own save and reboot workflow; consult the
  [HID Report Reference](../design/usb/hid-report-reference.md).
- If a setting is rejected, check that the bank role, clock profile, or I2C
  address is supported. A rejected request is not partially applied.

### A monitor reports no signal or an unexpected reading

- Confirm the bank is configured as a monitor and the signal is connected to
  the GPIO for the logical channel.
- Check the signal voltage, common ground, and frequency against the selected
  backend's measurement range.
- Monitor readings are sampled values, not waveform history. Rapidly changing
  signals may change between samples; see the relevant
  [backend design](../design/pwm-driver/).

For unresolved problems, record the board model, firmware version, interface,
configuration, command or host action, and observed result. Include whether the
issue reproduces after reconnecting with the default clock profile.
