# Control Protocol

PicoPWM exposes two command interfaces with the same logical data:

- **USB CDC serial** — text commands, easy for humans and scripts
- **I2C slave** — binary protocol, efficient for microcontrollers and host boards

Both can read device identity and per-channel properties. Both can also drive the same shared control path for channel updates and stop-all requests.

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

Read commands are answered from the realized channel snapshot published by the PWM driver layer. Write commands are captured in the I2C ISR, deferred into normal Core 0 polling, and then applied through the same shared control path used by the USB CDC CLI.

### Command Bytes

| Register / Command | Value | Write Length | Read Length | Description |
|--------------------|-------|--------------|-------------|-------------|
| `REG_INFO` | `0x00` | 1 | variable | Device type string, e.g. `PicoPWM` |
| `REG_VERSION` | `0x01` | 1 | variable | Version string, e.g. `1.0.0` |
| `REG_CHANNELS` | `0x02` | 1 | 1 | Channel count (24) |
| `REG_GET_CH0`..`REG_GET_CH23` | `0x10`..`0x27` | 1 | 12 | Read one channel's realized properties |
| `REG_SET_CH0`..`REG_SET_CH23` | `0x30`..`0x47` | 9 | 1 | Write one channel's frequency and duty, read back last status |
| `REG_SET_FREQ_CH0`..`REG_SET_FREQ_CH23` | `0x50`..`0x67` | 5 | 1 | Write one channel's frequency, read back last status |
| `REG_SET_DUTY_CH0`..`REG_SET_DUTY_CH23` | `0x70`..`0x87` | 5 | 1 | Write one channel's duty, read back last status |
| `REG_STOP_ALL` | `0x90` | 1 | 1 | Stop all channels and read back last status |

### Channel Property Layout

For `0x10 .. 0x27` (12 bytes, little-endian):

| Byte | Size | Field | Type |
|------|------|-------|------|
| 0-3 | 4 | `freq` | `float32` (Hz) |
| 4-7 | 4 | `duty` | `float32` (0.0..1.0) |
| 8-11 | 4 | `pulse_count` | `uint32_t` |

### Write Payload Layouts

For `0x30 .. 0x47` (8 payload bytes after the register byte, little-endian):

| Byte | Size | Field | Type |
|------|------|-------|------|
| 0-3 | 4 | `freq` | `float32` (Hz) |
| 4-7 | 4 | `duty` | `float32` (0.0..1.0) |

For `0x50 .. 0x67` (4 payload bytes after the register byte, little-endian):

| Byte | Size | Field | Type |
|------|------|-------|------|
| 0-3 | 4 | `freq` | `float32` (Hz) |

For `0x70 .. 0x87` (4 payload bytes after the register byte, little-endian):

| Byte | Size | Field | Type |
|------|------|-------|------|
| 0-3 | 4 | `duty` | `float32` (0.0..1.0) |

### Write Status Byte

Write-capable registers return one status byte when read:

| Value | Meaning |
|-------|---------|
| `0` | `PWM_DRIVER_RESULT_OK` |
| `1` | `PWM_DRIVER_RESULT_BUSY` |
| `2` | `PWM_DRIVER_RESULT_INVALID` |
| `3` | `PWM_DRIVER_RESULT_UNAVAILABLE` |
| `4` | `PWM_DRIVER_RESULT_TIMEOUT` |
| `5` | `PWM_DRIVER_RESULT_APPLY_FAILED` |

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

**Set channel 0 frequency and duty**

```
Master write: [0x30, freq_le32, duty_le32]
Master read:  [0x01] or [0x00]
```

`0x01` means the request is still busy or queued when read immediately after the write transaction. After a short delay, the master can repeat a one-byte write of `0x30` followed by a read to fetch the latest status byte for that command register.

**Stop all channels**

```
Master write: [0x90]
Master read:  [status]
```

### Notes

- Multi-byte values are always **little-endian**, matching the native byte order of both RP2040 and RP2350.
- String responses include a null terminator. Allocate at least 8 bytes for `info` and 8 bytes for `version`.
- The I2C ISR only captures request bytes and serves prepared response bytes. Write commands are executed later from normal Core 0 polling.
- A write command can therefore report `busy` if read back immediately. The master should allow a small delay and then re-read the same command register to fetch the final result.
- `pulse_count` is read-only over I2C. It cannot be set or reset via this interface.
- `freq` and `duty` returned over I2C are the realized values published by the PWM driver layer.
