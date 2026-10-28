# USB CDC Shell

The PicoPWM USB CDC interface provides an interactive shell for human control
and simple host scripts. USB CDC supplies the byte transport; the generic
`shell.*` module provides line editing and dispatch; PicoPWM registers its
commands through `pwm_commands.*`.

## Connection

Connect the board over a USB data cable and open its CDC serial device. The
terminal's baud-rate setting does not affect USB packet timing; this shell does
not configure a physical UART. The firmware prints the command help when a host
connection is opened and displays the `pico> ` prompt.

Commands are entered one line at a time and responses end with CRLF (`\r\n`).
The interactive editor supports backspace, cursor movement, command history,
and Tab completion of command names.

## Command Overview

The commands are grouped here by purpose; the CLI syntax itself is flat, with
no group prefix. Use `help` in the shell for the complete firmware command
list.

| Group | Commands | Purpose |
| --- | --- | --- |
| Channel | `get`, `set`, `status` | Read one channel, change a generator channel, or inspect all channel states. |
| Board | `info`, `version`, `config`, `config set/save/clock/address`, `led`, `reboot` | Inspect the board, configure startup behavior, control the LED, or restart. |
| Shell | `help` | Show available commands and usage. |

## Command Details

### PWM Channel Commands

Implemented by `channel_commands.*`.

| Command | Description | Example |
| --- | --- | --- |
| `get <ch>` | Read one channel's realized properties. | `get 0` |
| `set <ch> <freq_hz>` | Set frequency in integer Hz with 50% duty. | `set 0 1000` |
| `set <ch> <freq_hz> <duty_percent>` | Set frequency in integer Hz and duty in percent. | `set 0 1000 25` |
| `status` | Show all configured channel states. | `status` |

### Board Commands

Implemented by `board_commands.*`.

| Command | Description | Example |
| --- | --- | --- |
| `info` | Show the device type and system clock. | `info` |
| `version` | Show the build-time firmware version. | `version` |
| `config` | Show running and target Bank A/B/C settings. | `config` |
| `config set <a|b|c> <gen|mon>` | Change one target bank role. | `config set a gen` |
| `config save` | Persist target settings; reboot to apply. | `config save` |
| `config clock <default\|150\|200\|250>` | Change the target system clock in MHz. | `config clock 200` |
| `config address <7-bit>` | Change the target I2C address. | `config address 0x42` |
| `led <on\|off>` | Set the board LED state. | `led on` |
| `reboot` | Restart the board and apply saved target configuration. | `reboot` |

### Shell Commands

Implemented by `shell.*` and registered by `pwm_commands.*`.

| Command | Description | Example |
| --- | --- | --- |
| `help` | Show the command list (`help <cmd>` shows one command). | `help` |

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
defaults to `50`. For generator `set` requests, Bank A supports `10 Hz..1 MHz`,
Bank B `1 Hz..1 MHz`, and Bank C `1 Hz..1 kHz`. A `0 Hz` request selects a
static output: `0%` holds the pin low and `100%` holds it high. These are
accepted request ranges, not accuracy guarantees. Monitor banks measure inputs
instead of driving outputs. A `set` on a generator takes effect immediately;
check the bank role and GPIO before applying a nonzero frequency.

## Responses

Successful operations return `OK` or a requested value. Invalid input and
failed operations return `ERR` with a short explanation.

```text
pico> set 0 1000 50
OK CH0 freq=1000 Hz duty=50%

pico> get 0
CH0: freq=1000 Hz, duty=50%, enabled=yes
```

The returned frequency and duty values are the realized channel state.
The `enabled` field is `yes` when the realized frequency is nonzero and `no`
when it is zero; a zero-frequency channel can still be held high at `100%` duty.

`config` prints both `running` and `target` Bank A/B/C settings. `config set`
changes only the target configuration. `config save` writes the validated target
to flash; use `reboot` to apply it. The save disables interrupts and locks the
PWM core for one flash sector, so CDC, HID, and I2C stall until it finishes.
The stall ends when the sector program returns or the 5 second watchdog resets the board.
Allow 2 seconds.
See [Control Interfaces](README.md) for shared semantics and [I2C Protocol](i2c_protocol.md)
for the binary register protocol.
