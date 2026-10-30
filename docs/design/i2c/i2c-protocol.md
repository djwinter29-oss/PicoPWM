# I2C PWM Control Protocol

This document defines the I2C interface for reading PWM channel state and
controlling generator outputs. I2C does not configure bank roles, change the
slave address, save configuration, or control unrelated device functions.
Configure bank roles and other device settings through USB CDC or HID.

This is a fresh register-map design. It intentionally replaces the legacy
register assignments; firmware and hosts must use matching protocol support.

## Electrical and Bus Configuration

The board acts as an I2C slave on I2C1 at the default 7-bit address `0x40`.
The peripheral's internal timing is configured for 400 kHz; the I2C controller
sets the actual bus clock. Use a 100 kHz or 400 kHz controller clock.

External pull-up resistors are required for reliable bus operation; 4.7 kOhm
is typical. The firmware enables the MCU's internal pull-ups, but they are not
a replacement for external pull-ups. See [Hardware Setup](../../guides/hardware-setup.md)
for the SDA and SCL pins.

## Address Map

This is a byte-addressed register-selection protocol, not a memory window.
Each register has its own read or command semantics.

The generic I2C driver only frames bus transactions and transports response
bytes. Register selection, frame-length validation, command status, and
deferred PWM execution belong to the PWM protocol layer.
For the implementation boundary and callback rules, see the
[I2C Slave Driver Design](i2c-slave-driver-design.md).
The realized-state snapshot read may briefly stretch SCL while it acquires a
short cross-core lock.

| Register | Address | Write length | Read length | Purpose |
| --- | ---: | ---: | ---: | --- |
| `REG_INFO` | `0x00` | 1 | 7 | Protocol signature, firmware major, channel count, and roles |
| `REG_STOP_ALL` | `0x01` | 1 | 1 | Stop all generator outputs; read command status |
| `REG_GET_CHk` | `0x10 + k` | 1 | 5 | Read realized state of channel `k` |
| `REG_SET_CHk` | `0x30 + k` | 6 | 1 | Request frequency and duty for channel `k`; read status |

`k` is a logical channel ID in the range `0..n-1`, where `n` is the value
reported by `REG_INFO`. The standard profile exposes 24 channels: read
registers `0x10..0x27` and write registers `0x30..0x47`.
Addresses not listed here are reserved and outside the protocol contract.

The first two addresses provide protocol/device identification and stop-all,
followed by separate channel-read and channel-command ranges. The addresses
are not compatible with the legacy I2C map.

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

Any firmware release that changes the I2C contract incompatibly must increment
the firmware major version. Firmware releases with the same major version
preserve I2C compatibility. The major byte is the same major component used
in the firmware version exposed by the firmware's other interfaces. Hosts
must reject an unrecognized signature or unsupported major version; no
separate protocol version is maintained.

Major version `0` denotes development firmware and does not promise a stable
I2C contract. Hosts intended for released firmware should reject major `0`
unless they explicitly support a development build.

All operations defined by this register map are required for a firmware major
version that supports the map; there is no optional-capabilities field. The
signature is at the first register address so hosts can check compatibility
before interpreting the rest of the record. Legacy firmware's response at
this address does not begin with the `PPWM` signature.

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

A one-byte write requests every configured generator channel to stop at
`0 Hz / 0%`, holding its output low. Monitor banks are unchanged. The command
is queued and returns its completion status when read. If stopping one or more
channels fails, some outputs may already have stopped while others remain
active; read channel state to verify.

### `REG_GET_CHk` (`0x10 + k`)

Returns the latest realized state for channel `k` as five bytes:

| Offset | Size | Field | Encoding |
| ---: | ---: | --- | --- |
| 0 | 4 | `freq_hz` | Unsigned 32-bit Hz, little-endian |
| 4 | 1 | `duty` | Unsigned percent, `0..100` |

For generator channels, this is the applied output state, not merely the last
request. For monitor channels, it is the latest measurement. If a PIO or
software monitor cannot produce a stable sample, it reports the sentinel
`freq_hz = 0x0FFFFFFF`, `duty = 0`; hosts must treat that pair as invalid
measurement data. A stable static input is reported as `0 Hz` with duty `0%`
or `100%`. Each five-byte response is copied from one channel-state snapshot
captured at the start of that I2C read transaction; bytes in one response
cannot mix different updates. The snapshot is the latest state available at
that instant, not a promise that a monitor has just completed a new sample.
The register has no sample timestamp or freshness guarantee.

### `REG_SET_CHk` (`0x30 + k`)

The six-byte write consists of the register byte followed by this five-byte
payload:

| Payload offset | Size | Field | Encoding |
| ---: | ---: | --- | --- |
| 0 | 4 | `freq_hz` | Unsigned 32-bit Hz, little-endian |
| 4 | 1 | `duty` | Unsigned percent |

Duty values above `100` are rejected as `INVALID`, matching HID and the CDC shell.
For generator banks, frequency
`0` requests a static output: duty `100` sets it high and any lower duty sets
it low. At nonzero frequency, duty `0` and `100` also request static low and
high outputs. Readback reports the realized static level as `0 Hz / 0%` for
static low and `0 Hz / 100%` for static high, regardless of the requested
frequency. For other accepted generator requests, readback reports the
realized frequency and duty.

Nonzero generator frequency limits depend on the bank:

| Bank | Backend | Accepted request range |
| --- | --- | --- |
| A | Hardware PWM | 10 Hz-10 MHz (above 1 MHz experimental; reduced duty resolution) |
| B | PIO | 1 Hz-10 MHz (above 1 MHz experimental; coarse duty resolution) |
| C | Software PWM | 1 Hz-10 kHz (above 1 kHz experimental; coarse duty resolution and software jitter) |

These are accepted request ranges, not accuracy guarantees. Read back the
channel to inspect its realized frequency and duty; backend quantization can
make them differ from the request. A channel in a monitor bank cannot be driven
and returns `UNAVAILABLE`.

## I2C Transactions

### Register Reads

To read a register, write its one-byte address, then begin a read using a
repeated START or a STOP followed by a separate read:

```text
S  address+W  register  Sr  address+R  response bytes...  NACK  P
```

`S` is START, `Sr` is repeated START, `P` is STOP, and `NACK` is the master's
normal termination of a read. A register selection takes effect at the
repeated START or STOP ending the one-byte write. The selected register
remains available after STOP for the following pure read.
Writes that contain extra bytes or overflow the transport frame are malformed
and do not change the selected register.

The read length is chosen by the master. A shorter read returns a prefix of
the response; bytes requested beyond the response length are zero-padded.
Before a register has been selected, read bytes are zero.

For `REG_INFO`, read exactly seven bytes. Reads longer than the response are
zero-padded as specified above.

### Command Writes and Status Reads

Write lengths in the address map include the register byte. Send one complete
command per write transaction. The command is committed only at STOP or
repeated START, and only if the transaction contains exactly the listed
number of bytes. Except for the channel-command status-select shortcut below,
a short or overlong command is not executed and sets that command register's
status to `INVALID`; retransmit the complete command in a new transaction.
Bytes beyond the expected length in the same transaction must not be
interpreted as another register or command.

A complete command write selects its register for a following pure status
read. A one-byte write to a channel command register selects its status
without submitting another command and does not change its status. Do not use
that status-select shortcut for `REG_STOP_ALL`: its one-byte write executes
the command, so poll its status with a pure read instead. A one-byte write to
`REG_INFO` or another read-only register only selects that register; it never
changes device state.

An I2C ACK confirms receipt of bytes at the bus level; it does not confirm
that a command was accepted or applied. Use the command's status response.

## Status and Retry Behavior

Status is one unsigned byte, maintained per command register and representing
the most recent command attempt, including malformed writes. The first read
after a valid command may return `BUSY` while it is queued or being applied.
Poll with pure reads until a final status is returned. A one-byte
channel-command status-select write does not create an attempt or change the
status.

| Value | Name | Meaning |
| ---: | --- | --- |
| `0` | `OK` | Command completed successfully |
| `1` | `BUSY` | Command is queued or still being applied |
| `2` | `INVALID` | Command parameters or payload are invalid |
| `3` | `UNAVAILABLE` | Operation is unavailable or the shared write queue is full |
| `4` | `TIMEOUT` | A channel update was not claimed for application within the 1-second PWM apply deadline and was cancelled |
| `5` | `APPLY_FAILED` | Backend accepted the command but failed to apply it |
| `6` | `NOT_RUN` | No complete command attempt has been recorded for this register since startup |

Each command register's status starts as `NOT_RUN`. When a
command is rejected because the shared write queue is full, its register
reports `UNAVAILABLE`. The queue is four deep and is drained only from the Core 0
main loop. While Core 0 is inside a PWM apply it keeps USB CDC moving and does
not run HID, CLI, or I2C command execution, so a burst of writes during that
stall fills the queue and further writes report `UNAVAILABLE`. Wait before
retrying so the host can avoid sustained queue pressure. A host timeout does
not cancel a command Core 1 has already claimed.

The 1-second apply deadline starts when Core 0 begins processing a channel
update, not when the I2C transaction is received. Queue wait time is additional
and has no fixed upper bound. Hosts should poll status no more frequently than
once every 10 ms while it is `BUSY`, and should use an overall host-side
watchdog rather than assuming a fixed end-to-end completion time. If that
watchdog expires, do not blindly resend: read the status and realized channel
state first. `REG_STOP_ALL` applies stops sequentially. It attempts every
generator channel even if an earlier stop fails, then returns the first
non-`OK` channel result in ascending channel order. This includes `TIMEOUT` if
an individual stop was not claimed before its one-second apply deadline.

Because status is per register and there is no transaction ID, hosts must send
at most one outstanding command per command register. Firmware does not reject
same-register writes while another attempt is queued or being applied: valid
writes are each queued and executed in FIFO order. The status reports only the
most recent attempt, so it cannot identify which queued command completed if
the host violates this constraint.
A newer attempt replaces that register's status; an older completion cannot
overwrite the newer status. A malformed short or overlong write leaves
`INVALID` as the status for that attempt. Status is volatile and resets to
`NOT_RUN` on device startup or reboot. After a reset or lost connection, treat
any prior command completion as unknown and reconcile with the realized
channel state before retrying.

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

## Compatibility and Host Guidance

- This design replaces the legacy I2C register assignments. Legacy firmware
  does not implement this map; use a firmware build that explicitly supports
  this protocol before sending commands.
- Read `REG_INFO` and verify the `PPWM` signature and supported firmware major
  version before using the map. The map has no optional capabilities; all its
  operations are required for a supported major version. Treat major `0` as
  development firmware unless explicitly supported. Do not infer support from
  a device name alone.
- Bank roles and other device settings are configured through USB CDC or HID,
  not I2C.
- Multi-byte integers are little-endian, independent of host or MCU byte order.
- Read channel state to verify realized output or measurement values.
- Do not use reserved addresses or depend on undocumented register behavior.
