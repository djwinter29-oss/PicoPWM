# Configure the Board

Set bank roles, clock, and I2C address, or try a first output.
Commands below run in the board's USB CDC terminal.

- [Bank roles](#bank-roles)
- [Change settings](#change-settings)
- [First generator output](#first-generator-output)
- [PWM operating ranges](#pwm-operating-ranges)
- [Clock profiles](#clock-profiles)

## Bank Roles

Bank A uses hardware PWM (channels 0-7), B uses PIO (8-15), and C uses software
PWM (16-23). GPIOs are fixed; see the [pin map](hardware-setup.md#pwm-channels).
All banks default to monitor. Changing a bank's role requires a reboot.

## Change Settings

Read running settings with `config get`. Before a configuration save and
reboot, stop any active generators and verify their state:

```text
pwm stop all
pwm get all
```

Confirm every generator reports `0 Hz / 0%` and is disabled. If stop fails or
the state is uncertain, disconnect board power and do not save.

Change one or more settings with `config set`, for example:

```text
config set banka=generator, address=64
```

CDC validates the complete assignment set, saves, and reboots automatically.
In a dashboard, edit **Boot configuration** and confirm **Save & Reboot**.
Reconnect and check the running settings. I2C cannot change configuration.

## First Generator Output

Connect a USB data cable and open the board's serial device:
`/dev/ttyACM*` on Linux, `COMx` on Windows, or `/dev/cu.usbmodem*` on macOS.
Baud rate does not affect USB CDC. At the `pico> ` prompt, check:

```text
board get version
config get
```

Before saving a configuration, stop any active generators and verify their
state:

```text
pwm stop all
pwm get all
```

Confirm every generator reports `0 Hz / 0%` and is disabled. If stop fails or
the state is uncertain, disconnect board power and do not continue.

Connect a 3.3 V-safe oscilloscope or logic analyzer
to **GPIO 1 (physical pin 2 on Pico/Pico 2) and GND**, then run:

```text
config set banka=generator
```

This saves the setting and reboots automatically. Reconnect, run `config get`,
and confirm Bank A is a generator before continuing:

```text
config get
```

Then run:

```text
pwm set 0 1000 50
pwm get 0
```

Example readback; timing quantization may change the values:

```text
CH0: freq=1000 Hz, duty=50%, enabled=yes
```

Readback is not an instrument measurement. **When finished**, stop all generators:

```text
pwm stop all
pwm get all
```

Confirm every generator reports `0 Hz / 0%` and is disabled. If stopping
fails or the state is uncertain, disconnect board power. Stopping preserves
saved roles. To restore Bank A to monitoring, run
`config set banka=monitor`; it saves and reboots.
For problems, see [Troubleshooting](troubleshooting.md).

## PWM Operating Ranges

Accepted ranges are firmware request limits. Recommended ranges are guidance,
**not hardware-qualified accuracy specifications**.

| Backend | Accepted periodic generation | Recommended generation | Recommended measurement |
| --- | --- | --- | --- |
| Hardware PWM (Bank A) | 10 Hz-10 MHz | 10 Hz-1 MHz | 100 Hz-1 MHz |
| PIO (Bank B) | 1 Hz-10 MHz | 1 Hz-100 kHz | 10 Hz-100 kHz |
| Software PWM (Bank C) | 1 Hz-10 kHz | 1 Hz-1 kHz | 10 Hz-1 kHz |

Duty is an integer from 0% to 100%. At `0 Hz`, duty below 100% holds the pin
low; 100% holds it high. At nonzero frequency, endpoint duties also produce
static low/high. Timing is quantized, and PIO duty readback may differ from
the physical signal. Use an instrument when accuracy matters.

Start PIO and software measurement at 10 Hz; lower frequencies are experimental.

## Clock Profiles

Defaults are 125 MHz on RP2040 and 150 MHz on RP2350. Explicit `150` MHz
overclocks RP2040; `200` and `250` MHz overclock both MCUs and are not qualified
for every board.
Dashboards require acknowledgement for every explicit profile; HID requires
it above 150 MHz. CDC has no separate acknowledgement.

Flash saves can stall CDC, HID, and I2C for up to two seconds.
If the board no longer boots, use the
[recovery procedure](troubleshooting.md#recover-an-unbootable-configuration-or-reset-flash).
