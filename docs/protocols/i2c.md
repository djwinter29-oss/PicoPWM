# I2C PWM Control Protocol

I2C reads channel state and controls generators. Use USB CDC or HID for bank
roles, slave address, configuration saves, and other board functions.

## Electrical and Bus Configuration

The I2C1 slave defaults to 7-bit address `0x40`. Internal timing is configured
for 400 kHz; the controller sets the actual bus clock to 100 or 400 kHz.

Connect SDA to GPIO 26, SCL to GPIO 27, and common GND. Signals and pull-ups
must be **3.3 V**. Fit external pull-ups, typically 4.7 kOhm; internal pull-ups
are insufficient.

## Address Map

Registers select responses or commands, not a memory window. Channel reads
may briefly stretch SCL to capture a consistent snapshot.

| Register | Address | Write length | Read length | Purpose |
| --- | ---: | ---: | ---: | --- |
| [`REG_INFO`](#reg_info-0x00) | `0x00` | 1 | 7 | Protocol signature, firmware major, channel count, and roles |
| [`REG_STOP_ALL`](#reg_stop_all-0x01) | `0x01` | 1 | 1 | Stop all generator outputs; read command status |
| [`REG_GET_CHk`](#reg_get_chk-0x10--k) | `0x10 + k` | 1 | 5 | Read realized state of channel `k` |
| [`REG_SET_CHk`](#reg_set_chk-0x30--k) | `0x30 + k` | 6 | 1 | Request frequency and duty for channel `k`; read status |

`k` is a logical channel ID in the range `0..n-1`, where `n` is the value
reported by `REG_INFO`. The standard profile exposes 24 channels: read
registers `0x10..0x27` and write registers `0x30..0x47`.
Addresses not listed here are reserved and outside the protocol contract.

## Register Values

### `REG_INFO` (`0x00`)

Returns a fixed seven-byte record. Hosts must read and validate this register
before using any other register; legacy firmware will not return the `PPWM`
signature.

| Offset | Size | Field | Encoding |
| ---: | ---: | --- | --- |
| 0 | 4 | `signature` | ASCII bytes `PPWM` |
| 4 | 1 | `firmware_major` | Unsigned firmware major version (`uint8`) |
| 5 | 1 | `channel_count` | Unsigned channel count |
| 6 | 1 | `bank_roles` | Bitmask of active bank roles, using the bit assignments below |

Breaking I2C changes increment the firmware major version; released firmware
with the same major stays compatible. This is the firmware's major byte, not
a separate protocol version. Reject unknown signatures or unsupported majors.

Major version `0` denotes development firmware and does not promise a stable
I2C contract. Hosts intended for released firmware should reject major `0`
unless they explicitly support a development build.

All mapped operations are required; there is no optional-capabilities field.

`bank_roles` describes active, running roles; it does not report unsaved
target settings.

| Bit | Bank | Meaning when set |
| ---: | --- | --- |
| 0 | A | Generator |
| 1 | B | Generator |
| 2 | C | Generator |
| 3-7 | Reserved | Read as zero |

A clear bit means that bank is configured as a monitor. For example, `0x05`
means Banks A and C are generators and Bank B is a monitor.

### `REG_STOP_ALL` (`0x01`)

A one-byte write queues stop for all generators, holding outputs low at
`0 Hz / 0%`; monitors are unchanged. Read completion status. Failure can mean
a partial stop, so verify channel state.

### `REG_GET_CHk` (`0x10 + k`)

Returns the latest realized state for channel `k` as five bytes:

| Offset | Size | Field | Encoding |
| ---: | ---: | --- | --- |
| 0 | 4 | `freq_hz` | Unsigned 32-bit Hz, little-endian |
| 4 | 1 | `duty` | Unsigned percent, `0..100` |

Generators report applied state; monitors report their latest measurement.
PIO/software monitors without a valid sample return `0 Hz / 0%`, identical
to static low. Stable static inputs report `0 Hz` at `0%` or `100%`.

Each response comes from one snapshot at read start, so its bytes cannot mix
updates. There is no validity flag, timestamp, or promise of a fresh sample.

### `REG_SET_CHk` (`0x30 + k`)

The six-byte write consists of the register byte followed by this five-byte
payload:

| Payload offset | Size | Field | Encoding |
| ---: | ---: | --- | --- |
| 0 | 4 | `freq_hz` | Unsigned 32-bit Hz, little-endian |
| 4 | 1 | `duty` | Unsigned percent |

Duty above `100` returns `INVALID`, as in HID/CDC. At frequency `0`, duty
`100` drives high and lower duties drive low. At nonzero frequency, duties
`0`/`100` also select static low/high. Static readback is always `0 Hz / 0%`
or `0 Hz / 100%`; periodic readback gives realized frequency and duty.

Accepted periodic requests are 10 Hz-10 MHz for Bank A, 1 Hz-10 MHz for B,
and 1 Hz-10 kHz for C. These limits are not accuracy guarantees.
Quantization can change readback. Monitor writes return `UNAVAILABLE`.

## I2C Transactions

### Register Reads

Write the one-byte register address, then read after repeated START or STOP:

```text
S  address+W  register  Sr  address+R  response bytes...  NACK  P
```

`S` = START, `Sr` = repeated START, `P` = STOP. The master's NACK ends a read.
Selection takes effect at repeated START or STOP and persists for a following
pure read. Extra bytes or frame overflow are malformed and do not change
selection.

The master chooses length: short reads return a prefix; extra bytes and
reads before selection return zeros. Read exactly seven bytes for `REG_INFO`.

### Command Writes and Status Reads

Write lengths include the register byte. Send one complete command per
transaction; it commits at STOP or repeated START only with the exact length.
Except for status selection below, short/overlong writes execute nothing and
set `INVALID`. Resend complete commands in a new transaction. Extra bytes
cannot become another command or register.

A complete command selects its register for pure status reads. A one-byte
channel-command write selects status without executing or changing status.
**Do not do this for `REG_STOP_ALL`: its one-byte write executes stop.**
Poll stop with pure reads. One-byte writes to read-only registers only select
them.

ACK confirms byte receipt, not command acceptance or application. Read status.

## Status and Retry Behavior

Each command register stores a one-byte status for its latest attempt,
including malformed writes. Valid commands may first return `BUSY`; poll
pure reads until final. Status-select writes are not command attempts.

| Value | Name | Meaning |
| ---: | --- | --- |
| `0` | `OK` | Command completed successfully |
| `1` | `BUSY` | Command is queued or still being applied |
| `2` | `INVALID` | Command parameters or payload are invalid |
| `3` | `UNAVAILABLE` | Operation is unavailable or the shared write queue is full |
| `4` | `TIMEOUT` | A channel update was not claimed for application within the 1-second PWM apply deadline and was cancelled |
| `5` | `APPLY_FAILED` | Backend accepted the command but failed to apply it |
| `6` | `NOT_RUN` | No complete command attempt has been recorded for this register since startup |

Bursts can fill the queue and return `UNAVAILABLE`; wait before retrying.
Queue wait has no fixed bound. While `BUSY`, poll no faster than every 10 ms
and use a host watchdog. Host timeouts do not cancel claimed commands.

There is no transaction ID: allow one outstanding command per register.
Valid writes queue in order, but newer attempts replace status and older
completions cannot overwrite it. Startup/reboot resets status to `NOT_RUN`.
After reset or connection loss, inspect realized state before retrying.

## Examples

**Read device information**

```text
S  address+W  0x00  Sr  address+R  'P' 'P' 'W' 'M' 0x00 0x18 0x05  NACK  P
```

This example uses development firmware major `0`, 24 channels, active roles
`0x05` (Banks A and C generators).

**Read channel 0 state**

```text
S  address+W  0x10  Sr  address+R  freq_le32  duty_u8  NACK  P
```

**Set channel 0 to 1 kHz at 50% duty and poll status**

```text
S  address+W  0x30  0xE8  0x03  0x00  0x00  0x32  P
S  address+R  status  NACK  P
```

`0x000003E8` is `1000` in little-endian form and `0x32` is `50`. The first
status may be `0x01` (`BUSY`); repeat the pure read without resending the
command until a final status is returned.

**Stop all generator outputs**

```text
S  address+W  0x01  P
S  address+R  status  NACK  P
```

Poll `REG_STOP_ALL` using pure reads; do not write `0x01` again to poll.
