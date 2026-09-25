# USB CDC CLI

The PicoPWM USB CDC interface provides an interactive text command line for
human control and simple host scripts. It uses the USB serial device exposed by
the firmware; there is no UART connection.

## Connection

Connect the board over a USB data cable and open its CDC serial device at
115200 baud. The firmware prints the command help when a host connection is
opened and displays the `pico> ` prompt.

Commands are entered one line at a time and responses end with CRLF (`\r\n`).
The interactive editor supports backspace, cursor movement, command history,
Tab completion, and common Ctrl-key editing shortcuts.

## Commands

| Command | Description | Example |
| --- | --- | --- |
| `help` | Show the command list. | `help` |
| `info` | Show the device type. | `info` |
| `version` | Show the build-time firmware version. | `version` |
| `get <ch>` | Read one channel's realized properties. | `get 0` |
| `set <ch> <freq>` | Set frequency with 50% duty. | `set 0 1000` |
| `set <ch> <freq> <duty%>` | Set frequency and duty percentage. | `set 0 1000 25` |
| `status` | Show all configured channel states. | `status` |
| `led <on\|off>` | Set the board LED state. | `led on` |
| `stop` | Stop all channels and restore default settings. | `stop` |
| `reboot` | Reboot the board. | `reboot` |

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
See [Control Protocol](protocol.md) for the shared semantics and I2C register
protocol.
