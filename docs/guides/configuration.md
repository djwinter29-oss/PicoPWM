# Configure the Board

Use this guide to select bank roles, the clock profile, and the I2C address.
For command syntax, see the [USB CDC shell guide](usb-cdc-shell.md). For
implementation and backend constraints, see [PWM Driver Configuration](../design/pwm/configuration.md).

## Bank Roles

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

## Change Settings

On the CDC shell, run `config get` to inspect effective running settings. To
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
Reference](../protocols/usb/hid-report-reference.md). The documented I2C
interface is limited to PWM channel reads, writes, status, and stop-all; see the
[I2C Protocol Reference](../protocols/i2c.md).

## Clock Profiles

The SDK default is 125 MHz on RP2040 and 150 MHz on RP2350. The `150` MHz
profile is an overclock only on RP2040. Dashboard Save & Reboot requires checkbox acknowledgement
for every explicit profile, including 150 MHz; SDK default does not require it.
The HID API requires acknowledgement above 150 MHz; CDC has no separate acknowledgement step.
The `200` and `250` MHz profiles are not qualified for every board.

Saving configuration writes flash and temporarily stalls CDC, HID, and I2C;
allow up to two seconds for the operation. If a saved clock profile prevents
startup, see [Recover an unbootable configuration](troubleshooting.md#recover-an-unbootable-configuration-or-reset-flash).
