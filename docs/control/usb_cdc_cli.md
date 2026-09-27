# USB CDC Shell

The PicoPWM USB CDC interface provides an interactive shell for human control
and simple host scripts. USB CDC supplies the byte transport; the generic
`shell.*` module provides line editing and dispatch; PicoPWM registers its
commands through `pwm_commands.*`.

## Connection

Connect the board over a USB data cable and open its CDC serial device at
115200 baud. The firmware prints the command help when a host connection is
opened and displays the `pico> ` prompt.

Commands are entered one line at a time and responses end with CRLF (`\r\n`).
The interactive editor supports backspace, cursor movement, command history,
Tab completion, and common Ctrl-key editing shortcuts.

## Command Groups

The command registry is split into the same groups as the firmware source.

### PWM Channel Commands

Implemented by `channel_commands.*`.

| Command | Description | Example |
| --- | --- | --- |
| `get <ch>` | Read one channel's realized properties. | `get 0` |
| `set <ch> <freq>` | Set frequency with 50% duty. | `set 0 1000` |
| `set <ch> <freq> <duty%>` | Set frequency and duty percentage. | `set 0 1000 25` |
| `status` | Show all configured channel states. | `status` |

### Board Commands

Implemented by `board_commands.*`.

| Command | Description | Example |
| --- | --- | --- |
| `info` | Show the device type. | `info` |
| `version` | Show the build-time firmware version. | `version` |
| `bank` | Show each bank's lock state (unlocked/generator/monitor). | `bank` |
| `bank <hw\|pio\|sw> <generator\|monitor>` | Lock one bank's role; one-shot until reboot. | `bank hw generator` |
| `led <on\|off>` | Set the board LED state. | `led on` |
| `stop` | Restore configured generator banks' safe defaults. | `stop` |
| `reboot` | Reboot the board. | `reboot` |

### Shell Commands

Implemented by `shell.*` and registered by `pwm_commands.*`.

| Command | Description | Example |
| --- | --- | --- |
| `help` | Show the complete command list. | `help` |

Channel numbers are logical IDs assigned by the firmware configuration. The
current firmware exposes channels `0..23`; their backend assignment is an
implementation detail and may change with the configuration.

## Responses

Successful operations return `OK` or a requested value. Invalid input and
failed operations return `ERR` with a short explanation.

```text
pico> set 0 1000 50
OK CH0 freq=1000 Hz duty=50%

pico> get 0
CH0: freq=1000 Hz, duty=50%, pulses=12, enabled=yes
```

`pulse_count` is read-only, monotonic from power-on, and is not reset by
`stop`. The returned frequency and duty values are the realized channel state.
For generator banks, `stop` disables outputs and restores configured defaults.
For monitor banks, `stop` leaves measured input channels unchanged.
See [Control Interfaces](README.md) for shared semantics and [I2C Protocol](i2c_protocol.md)
for the binary register protocol.
