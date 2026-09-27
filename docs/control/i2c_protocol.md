# I2C Protocol

The Pico acts as an I2C slave on **I2C1** at 7-bit address `0x40`. See
[Pinout](../pinout.md) for SDA and SCL assignments.

### Electrical

External pull-up resistors (typically 4.7 kΩ) are required on SDA and SCL. The firmware does enable the MCU internal pull-ups, but external pull-ups are recommended for reliable operation, especially at higher clock speeds.

### Transaction Format

All transactions are initiated by an I2C master. The protocol is **write-then-read**:

1. **Write phase**: master sends one register byte and, when required, its payload.
2. **Read phase**: master reads the response bytes.

Read commands are answered from the realized channel snapshot published by the PWM driver layer. Write commands are captured in the I2C ISR, deferred into normal Core 0 polling, and then applied through the same shared control path used by the USB CDC CLI.

## Global Register Map

Every transaction starts with one register byte. A write transaction may add
the payload shown below; a read transaction returns the response length shown
below. Channel register ranges use the channel count advertised by
`REG_CHANNELS`.

| Register | Address | Write length | Read length | Payload / response |
|--------------------|-------|--------------|-------------|-------------|
| `REG_INFO` | `0x00` | 1 | variable | Device name string, including null terminator |
| `REG_VERSION` | `0x01` | 1 | variable | Firmware version string, including null terminator |
| `REG_CHANNELS` | `0x02` | 1 | 1 | Logical channel count |
| `REG_BANK_STATE` | `0x03` | 1 | 3 | Per-bank lock state, one byte each for HW/PIO/SW: `0`=unlocked, `1`=generator, `2`=monitor |
| `REG_BANK_LOCK` | `0x04` | 3 | 1 | Two-byte `bank_id`/`role_id` payload; locks one bank, returns status |
| `REG_GET_CHk` | `0x10 + k` | 1 | 9 | Channel state: `freq`, `duty`, `pulse_count` |
| `REG_SET_CHk` | `0x30 + k` | 6 | 1 | Five-byte `freq`/`duty` payload; returns status |
| `REG_STOP_ALL` | `0x90` | 1 | 1 | Profile reset request; returns status |
| `REG_LED` | `0x91` | 2 | 1 | One-byte LED value; returns status |
| `REG_REBOOT` | `0x92` | 1 | 1 | Reboot request; returns status |

`bank_id` is `0`=HW, `1`=PIO, `2`=SW. `role_id` is `0`=generator, `1`=monitor.
Locking a bank is one-shot: `REG_BANK_LOCK` on an already-locked bank returns
an invalid status, and a channel in an unlocked bank rejects `REG_GET_CHk`/
`REG_SET_CHk` the same way an out-of-range channel would.

Here `n` is the channel count returned by `REG_CHANNELS` and `k` ranges from
`0` through `n - 1`. The current default profile exposes 24 channels, so the
ranges are `0x10..0x27` and `0x30..0x47`.

## Channel Property Layout

For `0x10 .. 0x27` (9 bytes, little-endian):

| Byte | Size | Field | Type |
|------|------|-------|------|
| 0-3 | 4 | `freq` | `uint32_t` (Hz) |
| 4 | 1 | `duty` | `uint8_t` (0..100) |
| 5-8 | 4 | `pulse_count` | `uint32_t` |

## Write Payload Layouts

For `0x30 .. 0x47` (5 payload bytes after the register byte, little-endian):

| Byte | Size | Field | Type |
|------|------|-------|------|
| 0-3 | 4 | `freq` | `uint32_t` (Hz) |
| 4 | 1 | `duty` | `uint8_t` (0..100, values above 100 are clamped) |

For `0x91` (1 payload byte after the register byte):

| Byte | Size | Field | Type |
|------|------|-------|------|
| 0 | 1 | `led_on` | `uint8_t` (`0` = off, `1` = on) |

## Write Status Byte

Write-capable registers return one status byte when read:

| Value | Meaning |
|-------|---------|
| `0` | `PWM_DRIVER_RESULT_OK` |
| `1` | `PWM_DRIVER_RESULT_BUSY` |
| `2` | `PWM_DRIVER_RESULT_INVALID` |
| `3` | `PWM_DRIVER_RESULT_UNAVAILABLE` |
| `4` | `PWM_DRIVER_RESULT_TIMEOUT` |
| `5` | `PWM_DRIVER_RESULT_APPLY_FAILED` |

## Examples

**Read device type**

```
Master write: [0x00]
Master read:  "PicoPWM" (7 bytes + null terminator = 8 bytes)
```

**Read channel 0**

```
Master write: [0x10]
Master read:  [freq_le32, duty_u8, pulse_count_le32] (9 bytes)
```

**Set channel 0 frequency and duty**

```
Master write: [0x30, freq_le32, duty_u8]
Master read:  [0x01] or [0x00]
```

Here `freq_le32` is a little-endian `uint32_t` in Hz and `duty_u8` is one byte representing duty percent.

`0x01` means the request is still busy or queued when read immediately after the write transaction. After a short delay, the master can repeat a one-byte write of `0x30` followed by a read to fetch the latest status byte for that command register.

**Stop all channels**

```
Master write: [0x90]
Master read:  [status]
```

**Set LED on**

```
Master write: [0x91, 0x01]
Master read:  [status]
```

**Reboot the board**

```
Master write: [0x92]
Master read:  [status]
```

## Notes

- Multi-byte values are always **little-endian**, matching the native byte order of both RP2040 and RP2350.
- String responses include a null terminator. Allocate enough space for the full version string plus the terminator.
- The I2C ISR only captures request bytes and serves prepared response bytes. Write commands are executed later from normal Core 0 polling.
- A write command can therefore report `busy` if read back immediately. The master should allow a small delay and then re-read the same command register to fetch the final result.
- `REG_REBOOT` follows the same deferred path, but the device may reset before a later status re-read is possible.
- `pulse_count` is read-only over I2C. It cannot be set or reset via this interface.
- `freq` and `duty` returned over I2C are the realized values published by the PWM driver layer.
- The public control API now uses integer Hz and integer duty percent rather than float inputs.
