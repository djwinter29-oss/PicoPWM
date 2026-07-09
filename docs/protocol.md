# Control Protocol

PicoPWM exposes two command interfaces with the same logical data:

- **USB CDC serial** — text commands, easy for humans and scripts
- **I2C slave** — binary protocol, efficient for microcontrollers and host boards

Both can read device identity and per-channel properties. In the current firmware, the only write-command ingress path is the USB CDC serial command interface; I2C is currently read-only.

**Important:** `pulse_count` is read-only from both interfaces. It cannot be set or reset directly. The `stop` command disables all channels by restoring `freq = 0 Hz` and `duty = 50%`, but `pulse_count` continues accumulating from power-on.

---

## USB CDC Serial Protocol

Connect to the Pico as a USB serial port (CDC) at **115200 baud**. Type commands and press Enter. Responses are line-terminated with `\r\n`.

### Serial Commands

| Command | Description | Example |
|---------|-------------|---------|
| `info` | Returns device type | `info` → `PicoPWM` |
| `version` | Returns firmware version | `version` → `1.0.0` |
| `get <ch>` | Read all properties of channel `ch` | `get 0` |
| `set <ch> f <freq>` | Set channel frequency (Hz) | `set 0 f 1000` |
| `set <ch> d <duty%>` | Set channel duty (0..100) | `set 0 d 50` |
| `h <ch> <freq> <duty%>` | Set hardware PWM channel (legacy) | `h 0 1000 50` |
| `p <ch> <freq> <duty%>` | Set PIO PWM channel (legacy) | `p 0 1000 50` |
| `s <ch> <freq> <duty%>` | Set software PWM channel (legacy) | `s 0 100 50` |
| `d <ch> <duty%>` | Set software PWM duty only (legacy) | `d 0 25` |
| `stop` | Stop all channels and reset to power-up defaults | `stop` |
| `status` | Print all channels | `status` |
| `help` | Show command list | `help` |

### Channel Numbers

- `0..7` = hardware PWM channels
- `8..15` = PIO PWM channels
- `16..23` = software PWM channels

For the hardware bank, the intended external pin mapping uses PWM slice **channel B** pins so the generator and monitoring firmware variants can share the same physical connector order on both Pico (RP2040) and Pico 2 (RP2350).

### Response Examples

```text
> get 0
CH 0: freq=1000.00 Hz, duty=50.00%, pulses=1234

> set 0 f 2000
OK

> status
CH 0: freq=2000.00 Hz, duty=50.00%, pulses=42
CH 1: freq=0.00 Hz, duty=50.00%, pulses=0
...

> info
PicoPWM
```

### Error Handling

Invalid commands print `ERR: <message>` and print a short help line. Valid commands print `OK` or the requested data.

---

## I2C Slave Protocol

The Pico acts as an I2C slave on **I2C0** using:

- **SDA**: GPIO 16
- **SCL**: GPIO 17
- **7-bit address**: `0x40`

### Electrical

External pull-up resistors (typically 4.7 kΩ) are required on SDA and SCL. The firmware does enable the MCU internal pull-ups, but external pull-ups are recommended for reliable operation, especially at higher clock speeds.

### Transaction Format

All transactions are initiated by an I2C master. The protocol is **write-then-read**:

1. **Write phase**: master sends one command byte.
2. **Read phase**: master reads the response bytes.

The I2C slave is currently **read-only**. To change a channel, use the USB CDC serial commands.

If write-capable I2C commands are added in a future revision, they should feed the same `control.c` to `pwm_driver_set_freq()` command path used by the CDC CLI.

### Command Bytes

| Command | Value | Write Length | Read Length | Description |
|---------|-------|--------------|-------------|-------------|
| `CMD_INFO` | `0x00` | 1 | variable | Device type string, e.g. `PicoPWM` |
| `CMD_VERSION` | `0x01` | 1 | variable | Version string, e.g. `1.0.0` |
| `CMD_CHANNELS` | `0x02` | 1 | 1 | Channel count (24) |
| `CMD_GET_CH0`..`CMD_GET_CH23` | `0x10`..`0x27` | 1 | 12 | Read one channel's properties |

### Channel Property Layout

For `0x10 .. 0x27` (12 bytes, little-endian):

| Byte | Size | Field | Type |
|------|------|-------|------|
| 0-3 | 4 | `freq` | `float32` (Hz) |
| 4-7 | 4 | `duty` | `float32` (0.0..1.0) |
| 8-11 | 4 | `pulse_count` | `uint32_t` |

### Examples

**Read device type**

```
Master write: [0x00]
Master read:  "PicoPWM" (7 bytes + null terminator = 8 bytes)
```

**Read channel 0**

```
Master write: [0x10]
Master read:  [freq_le32, duty_le32, pulse_count_le32] (12 bytes)
```

### Notes

- Multi-byte values are always **little-endian**, matching the native byte order of both RP2040 and RP2350.
- String responses include a null terminator. Allocate at least 8 bytes for `info` and 8 bytes for `version`.
- The I2C slave handler is interrupt-driven. The master may need a small delay between transactions.
- `pulse_count` is read-only over I2C. It cannot be set or reset via this interface.
- `freq` and `duty` returned over I2C are the realized values published by the PWM driver layer.
