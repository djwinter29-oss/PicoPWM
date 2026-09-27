# I2C Protocol

The Pico acts as an I2C slave on **I2C1**. See [Pinout](../pinout.md) for the
SDA and SCL GPIO assignments.

### Electrical

External pull-up resistors (typically 4.7 kΩ) are required on SDA and SCL. The firmware does enable the MCU internal pull-ups, but external pull-ups are recommended for reliable operation, especially at higher clock speeds.

### Transaction Format

All transactions are initiated by an I2C master. The protocol is **write-then-read**:

1. **Write phase**: master sends one register byte and, when required, its payload.
2. **Read phase**: master reads the response bytes.

Read commands are answered from the realized channel snapshot published by the PWM driver layer. The ISR only captures bytes. Core 0 builds the response buffer before the slave releases a stretched clock, so channel reads and string copies do not run in the ISR. Write commands are queued by the ISR and applied from Core 0 polling through the same shared control path used by the USB CDC CLI.

The firmware keeps four complete writes. A write that arrives while that queue
is full is dropped, and the register status becomes `PWM_DRIVER_RESULT_UNAVAILABLE`
immediately. That status describes the newest write to the register. An older
queued write finishing does not replace it. The status read finishes as a failure
and the master can send the write again. The fixed queue bounds ISR memory use.

SCL stretches only while Core 0 is building a response that a write already
requested. A read that arrives with no response requested returns one zero byte
and releases the clock.

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
| `REG_CONFIG` | `0x03` | 1 | 14 | Running then target backend/role pairs and I2C addresses |
| `REG_CONFIG_SET` | `0x04` | 4 | 1 | Payload: bank, backend, role; updates target only |
| `REG_CONFIG_SAVE` | `0x05` | 1 | 1 | Persists target; reboot applies it |
| `REG_CONFIG_ADDRESS` | `0x06` | 2 | 1 | One-byte target 7-bit I2C address; reboot applies it |
| `REG_GET_CHk` | `0x10 + k` | 1 | 9 | Channel state: `freq`, `duty`, `pulse_count` |
| `REG_SET_CHk` | `0x30 + k` | 6 | 1 | Five-byte `freq`/`duty` payload; returns status |
| `REG_STOP_ALL` | `0x90` | 1 | 1 | Profile reset request; returns status |
| `REG_LED` | `0x91` | 2 | 1 | One-byte LED value; returns status |
| `REG_REBOOT` | `0x92` | 1 | 1 | Reboot request; returns status |

Here `n` is the channel count returned by `REG_CHANNELS` and `k` ranges from
`0` through `n - 1`. The fixed Bank A/B/C allocation exposes 24 channels, so the
ranges are `0x10..0x27` and `0x30..0x47`.

`REG_CONFIG` returns 14 bytes: six bytes for the running configuration followed
by six bytes for the target configuration, then the running and target I2C
addresses. Each configuration uses one
backend/role pair per bank in A, B, C order. Backend values are `0`=HW,
`1`=PIO, `2`=SW; role values are `0`=generator and `1`=monitor.

`REG_CONFIG_SET` accepts three payload bytes: `bank`, `backend`, `role`. It
updates the target only. `REG_CONFIG_ADDRESS` accepts one usable 7-bit address byte
(`0x08..0x77`)
and updates the target only. `REG_CONFIG_SAVE` validates and persists the
target; the host must issue `REG_REBOOT` before the target becomes running.

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

`0x01` means the newest request is still busy or queued when read immediately after the write transaction. `0x03` (`PWM_DRIVER_RESULT_UNAVAILABLE`) means that newest write was dropped because four writes were already queued; that status stays in place until a newer attempt, and the master can send the write again. A later pure read, with no new register byte, returns the latest status. A one-byte write of `0x30` also selects that register for a status read and does not apply a new frequency or duty.

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
- A write command can therefore report `busy` if read back immediately. The master should allow a small delay and then read once, without writing the register again, to fetch the final result. Writing a one-byte command register again runs that command again.
- The slave queues up to four writes. A write that arrives while the queue is full is dropped and the register status becomes `PWM_DRIVER_RESULT_UNAVAILABLE` immediately. The master can send that write again. The status byte always tracks the newest attempt for that register.
- A read with no requested response returns `0x00` and releases SCL. The slave stretches SCL only while a requested response is still being built.
- `REG_REBOOT` follows the same deferred path, but the device may reset before a later status re-read is possible.
- `pulse_count` is read-only over I2C. It cannot be set or reset via this interface.
- `freq` and `duty` returned over I2C are the realized values published by the PWM driver layer.
- The public control API now uses integer Hz and integer duty percent rather than float inputs.
