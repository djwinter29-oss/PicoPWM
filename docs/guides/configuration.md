# Configure the Board

Use this guide to select bank roles, the clock profile, and the I2C address.
For command syntax, see the [USB CDC shell reference](../protocols/usb-cdc.md). For
implementation and backend constraints, see [PWM bank design](../design/pwm/banks.md).

## Bank Roles

Each bank has a fixed backend and logical channel range, and its role can be
selected as generator or monitor. See [Hardware Setup](hardware-setup.md) for
the authoritative channel-to-GPIO map.

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

CDC `config set` validates all assignments, saves the target, and reboots to
apply it. HID has a separate configuration workflow in the
[HID report reference](../protocols/usb-hid.md). I2C supports channel control
only; see the [I2C protocol](../protocols/i2c.md).

## Clock Profiles

The SDK default is 125 MHz on RP2040 and 150 MHz on RP2350. The `150` MHz
profile overclocks RP2040; `200` and `250` MHz are overclocks and are not
qualified for every board. Dashboard Save & Reboot asks for acknowledgement
for every explicit profile; HID requires it above 150 MHz. CDC has no separate
acknowledgement step.

Saving configuration writes flash and temporarily stalls CDC, HID, and I2C;
allow up to two seconds for the operation. If a saved clock profile prevents
startup, see [Recover an unbootable configuration](troubleshooting.md#recover-an-unbootable-configuration-or-reset-flash).
