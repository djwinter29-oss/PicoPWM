# USB CDC Shell

The PicoPWM USB CDC interface provides an interactive shell for human control
and simple host scripts.

## Connection

Connect the board over a USB data cable and open its CDC serial device. The
terminal's baud-rate setting does not affect USB packet timing; this shell does
not configure a physical UART. The firmware prints command help when a host
connection is opened and displays the `pico> ` prompt.

Commands are entered one line at a time and responses end with CRLF (`\r\n`).
The interactive editor supports backspace, cursor movement, command history,
and Tab completion of command names.

## Command Format

Commands follow the `group verb parameters` format. The group identifies the
target (`pwm`, `board`, or `config`); the verb describes the operation.

| Group | Commands | Purpose |
| --- | --- | --- |
| PWM | `pwm get <channel>`, `pwm get all`, `pwm set <channel> <freq_hz> [duty_percent]`, `pwm stop all` | Read realized channel state, set a generator channel, or stop every generator output. |
| Board | `board get <clock\|led\|version>`, `board set led <on\|off>`, `board reboot` | Read board state, control the LED, or restart. |
| Configuration | `config get [banka\|bankb\|bankc\|clock\|address]`, `config set <key=value>[, <key=value>...]` | Read configuration or update several settings together. |
| Help | `help [group]`, `?` | List commands or show contextual options and syntax. |

### PWM Commands

| Command | Description | Example |
| --- | --- | --- |
| `pwm get <channel>` | Read one channel's realized frequency and duty. | `pwm get 0` |
| `pwm get all` | Read all channel states. | `pwm get all` |
| `pwm set <channel> <freq_hz> [duty_percent]` | Set a generator channel; duty defaults to 50%. | `pwm set 0 1000 50` |
| `pwm stop all` | Stop every generator output at 0 Hz / 0% duty; monitor channels remain unchanged. | `pwm stop all` |

The stop command attempts every generator channel even if one channel fails.
If it reports an error, some outputs may have stopped while others may remain
active; inspect the channels with `pwm get all` before relying on their state.

The `pwm get all` listing is bounded to the configured channel count and is
buffered by the CDC transport before delivery. Keep the terminal connected
while the response is being transmitted.

Channel numbers are logical IDs. Their backend and GPIO are fixed by bank;
configuration changes only the bank's role (generator or monitor).

| Bank | Channels | Backend | GPIOs |
| --- | --- | --- | --- |
| A | `0..7` | Hardware PWM | `1, 3, 5, 7, 9, 11, 13, 15` |
| B | `8..15` | PIO PWM | `0, 2, 4, 6, 8, 10, 12, 14` |
| C | `16..23` | Software PWM | `16, 17, 18, 19, 20, 21, 22, 28` |

For example, channel `0` is GPIO 1. See [Hardware Setup](../hardware-setup.md)
for the full pin map and board-specific notes.

The duty argument is an integer percentage from `0` to `100`; if omitted it
defaults to `50`. Generator request ranges are Bank A `10 Hz..1 MHz`, Bank B
`1 Hz..1 MHz`, and Bank C `1 Hz..1 kHz`. A `0 Hz` request selects a static
output: `0%` holds the pin low and `100%` holds it high. These are accepted
request ranges, not accuracy guarantees. Monitor banks measure inputs instead
of driving outputs. A nonzero `pwm set` request takes effect immediately, so
check the bank role and GPIO before applying one.

### Board Commands

| Command | Description | Example |
| --- | --- | --- |
| `board get clock` | Read the running system clock and whether its target was met. | `board get clock` |
| `board get led` | Read the LED state. | `board get led` |
| `board get version` | Read the build-time firmware version. | `board get version` |
| `board set led <on\|off>` | Set the board LED state. | `board set led on` |
| `board reboot` | Restart the board. | `board reboot` |

### Configuration Commands

| Command | Description | Example |
| --- | --- | --- |
| `config get` | Show all running and target settings. | `config get` |
| `config get <item>` | Show one setting's running and target values. Valid items: `banka`, `bankb`, `bankc`, `clock`, `address`. | `config get clock` |
| `config set <key=value>[, <key=value>...]` | Validate and apply all assignments together, save them, and reboot to apply. | `config set banka=generator, clock=150, address=64` |

The setting names are `banka`, `bankb`, `bankc`, `clock`, and `address`.
Banks accept `generator` or `monitor`; the system clock accepts `default`,
`150`, `200`, or `250` MHz; the 7-bit I2C address accepts decimal values
`8..119`. A single `config set` may contain multiple comma-separated
assignments. The complete set is validated before any setting is changed; a
rejected assignment does not partially apply the request. A successful command
saves the new target configuration and reboots the board; no separate save or
reboot command is needed.

Configuration changes do not affect running hardware until the reboot applies
the saved target. Saving uses flash and temporarily stalls CDC, HID, and I2C
while the flash sector is programmed; the operation can take up to 2 seconds.

### Help

`help` is the canonical help command. Use `help <group>` for a group's command
summary. Enter `?` by itself for the top-level list, or append it to a partial
command to see valid syntax and options (for example, `pwm get ?`). Help is
informational only and never changes device state.

| Input | Result |
| --- | --- |
| `help` or `?` | List command groups and their forms. |
| `help pwm` | List PWM commands. |
| `pwm ?` | Show PWM verbs and their options. |
| `pwm get ?` | Show the channel or `all` option. |
| `config set ?` | Show accepted setting keys and values. |

Incomplete or invalid commands should show the relevant syntax and choices
rather than only a generic error.

## Responses

Successful operations return `OK` or a requested value. Invalid input and
failed operations return `ERR` with a short explanation. `pwm get` reports
realized channel state, not just the last requested values. The `enabled` field
is `yes` when the realized frequency is nonzero and `no` when it is zero; a
zero-frequency channel can still be held high at `100%` duty.

See [Control Interfaces](README.md) for shared semantics and
[I2C Protocol](i2c_protocol.md) for the binary register protocol.
