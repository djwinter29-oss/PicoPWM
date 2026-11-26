# USB CDC Shell

USB CDC accepts interactive text commands and simple host scripts.

## Connection

Connect a USB data cable and open `/dev/ttyACM*` (Linux), `COMx` (Windows), or
`/dev/cu.usbmodem*` (macOS). No dashboard is needed. Baud rate does not affect
USB timing or configure a UART. Connection prints help and the `pico> ` prompt.
A clock-configuration failure prints a warning; read `board get clock` for
the actual clock.

Disconnect clears the editor, incomplete input, and pending responses in
application/USB queues. It does not cancel a command already running.

Enter one command per line; responses end with CRLF (`\r\n`). The editor
supports backspace, cursor movement, history, and Tab completion.
**Lines are limited to 100 characters.** Extra characters are ignored, so an
overlong line can execute its accepted prefix.

## Command Format

Format: `group verb parameters`. Groups: `pwm`, `board`, `config`, `help`.

### PWM Commands

| Command | Description | Example |
| --- | --- | --- |
| `pwm get <channel>` | Read one channel's realized frequency and duty. | `pwm get 0` |
| `pwm get all` | Read all channel states. | `pwm get all` |
| `pwm set <channel> <freq_hz> [duty_percent]` | Set a generator channel; duty defaults to 50%. | `pwm set 0 1000 50` |
| `pwm stop all` | Stop every generator output at 0 Hz / 0% duty; monitor channels remain unchanged. | `pwm stop all` |

Stop attempts every generator even after a failure. An error can mean a
partial stop; verify with `pwm get all`.
Keep the terminal connected until a listing finishes: output is queued as it
is produced. Rerun an incomplete listing.

Channels, backends, and GPIOs are fixed; configuration changes bank roles only.
See the [pin map](../guides/hardware-setup.md#pwm-channels).

Duty is an integer `0..100`, default `50`. Accepted periodic requests are 10 Hz-10 MHz for Bank A,
1 Hz-10 MHz for B, and 1 Hz-10 kHz for C. These are request limits, not
accuracy guarantees; see [operating ranges](../guides/configuration.md#pwm-operating-ranges).
At `0 Hz`, duty below `100%` holds the pin low and `100%` holds it high.
Nonzero requests take effect immediately; check the role and GPIO first.

### Board Commands

| Command | Description | Example |
| --- | --- | --- |
| `board get clock` | Read the actual running system clock in Hz. | `board get clock` |
| `board get led` | Read the LED state. | `board get led` |
| `board get version` | Read the build-time firmware version. | `board get version` |
| `board set led <on\|off>` | Set the board LED state. | `board set led on` |
| `board reboot` | Restart the board. | `board reboot` |

### Configuration Commands

| Command | Description | Example |
| --- | --- | --- |
| `config get` | Show all effective running settings. | `config get` |
| `config get <item>` | Show one setting's effective running value. Valid items: `banka`, `bankb`, `bankc`, `clock`, `address`. | `config get clock` |
| `config set <key=value>[, <key=value>...]` | Set one or more configuration values. | `config set banka=generator, clock=150, address=64` |

Banks accept `generator` or `monitor`; clock accepts `default`, `150`, `200`,
or `250` MHz; the 7-bit I2C address accepts decimal `8..119`.
Comma-separated assignments are validated together before any change.
Invalid input applies nothing. Success saves and reboots automatically.
`OK config saved; rebooting` confirms persistence, not the running clock.

All `get` responses report actual running values, not saved targets. For example:

```text
pico> board get clock
clock=125000000 Hz
pico> config get address
address=64
pico> config get
banka=monitor
bankb=monitor
bankc=monitor
clock=125000000 Hz
address=64
```

Example values vary with board and configuration.

Use `default` unless you intend to overclock: 150 MHz overclocks RP2040,
and 200/250 MHz are not qualified for every board. CDC does not require a
separate overclock acknowledgement. A failed boot can require BOOTSEL recovery.

### Help

`help` and `?` show syntax and choices without changing state:

| Input | Result |
| --- | --- |
| `help` | List registered commands and their syntax. |
| `?` | List command groups and how to get their syntax. |
| `help pwm` | List PWM commands. |
| `pwm ?` | Show PWM verbs and their options. |
| `pwm get ?` | Show the channel or `all` option. |
| `config set ?` | Show accepted setting keys and values. |

Incomplete or invalid commands show relevant syntax and choices.

## Responses

Success returns `OK` or a value; invalid input and failures return `ERR` with
an explanation. `pwm get` reports realized state. `enabled` is `yes` for
nonzero frequency and `no` at zero; a zero-frequency pin can still be high
at `100%` duty.
