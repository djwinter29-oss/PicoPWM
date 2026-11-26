# USB HID Reports

One vendor-defined HID interface runs alongside CDC. All reports are feature
reports; ID `0` is unused.

## Host Polling and Validation

1. Read report 1 and validate product ID, version, and exact length before sending requests. Cache identity and
   `clock_profile_mask` for the connection.
2. Read report 2 to inspect the running and target configuration.
3. Poll reports 10-12 for channel state as needed. Each bank is a separate read; the three reports do not form one
   atomic capture.
4. Submit actions through report 20, then GET report 20 until `status` is not `PENDING` and `transaction_id` and
   `operation` match. Use one controlling host.
5. After a successful mutation, reread the affected channel or configuration report.

Read full report lengths. Validate type, ID, exact length, version, reserved
fields/bits, and enums. Read errors invalidate only affected data.
On disconnect/restart, discard caches and pending assumptions; reread identity
and configuration. Saves can pause service/communication for two seconds;
host timeouts do not prove failure.

## USB Identity

Raspberry Pi has allocated USB PID `0x116C` under its VID `0x2E8A` to PicoPWM through the
[USB PID program](https://github.com/raspberrypi/usb-pid).

| Identifier | Value | Notes |
| ---------- | ----- | ----- |
| USB VID | `0x2E8A` | Raspberry Pi vendor ID. |
| USB PID | `0x116C` | Allocated to PicoPWM by Raspberry Pi. |
| HID `product_id` | `0x0003` | PicoPWM protocol identifier. |
| HID usage | page `0xFF00`, usage `0x01` | Vendor-defined usage. |

The USB manufacturer and product strings are `djwinter` and `PicoPWM`, respectively. The HID interface
string, shown by host device pickers, is `picopwm-<serial number>`.

Validate identity `product_id` before decoding other reports; it is not the USB PID.

## Report IDs

| Report ID | Name | Direction | Payload | Purpose |
| --------- | ---- | --------- | ------: | ------- |
| `1` | [Device Identity & Capabilities](#report-id-1-device-identity--capabilities) | Host reads | 18 bytes | Identity, firmware version, capabilities. |
| `2` | [Board Configuration](#report-id-2-board-configuration) | Host reads | 21 bytes | Running/target settings and clock profile. |
| `3` | [Firmware Memory](#report-id-3-firmware-memory) | Host reads | 9 bytes | SRAM reservation and capacity. |
| `4` | [Runtime Status](#report-id-4-runtime-status) | Host reads | 13 bytes | Uptime, temperature estimate, service load. |
| `5` | [Pin Assignment](#report-id-5-pin-assignment) | Host reads | 28 bytes | Channel, I2C, and LED GPIOs. |
| `10`-`12` | [Bank Status — Bank A–C](#reports-10-12-bank-status) | Host reads | 41 bytes | Realized state of eight channels per bank. |
| `20` | [Board Action & Result](#report-id-20-board-action--result) | Host reads/writes | 12 bytes | SET submits an action; GET reads its latest result. |

**Wire rules:** multi-byte integers are little-endian, matching the I2C protocol. Offsets and sizes exclude the
transport report-ID byte. All layouts use version `1`, independent of firmware version. Report type, ID, length,
and version identify a layout within the selected product contract.

## Report ID 1: Device Identity & Capabilities

Feature report, host reads, 18-byte payload.

| Offset | Size | Field | Meaning |
| -----: | ---: | ----- | ------- |
| 0 | 1 | `version` | Layout version, `1`. |
| 1 | 2 | `product_id` | `0x0003` for PicoPWM. |
| 3 | 8 | `device_uid` | SDK board ID, in USB serial-string order. |
| 11 | 1 | `firmware_major` | Firmware version major. |
| 12 | 1 | `firmware_minor` | Firmware version minor. |
| 13 | 1 | `firmware_patch` | Firmware version patch. |
| 14 | 1 | `mcu` | `1` RP2040, `2` RP2350. |
| 15 | 1 | `channel_count` | Number of logical channels exposed by this firmware; `24` (three banks of eight). |
| 16 | 1 | `capabilities` | Capability flags; see the table below. |
| 17 | 1 | `clock_profile_mask` | Bit *n* is set when profile *n* is supported; bits 5-7 are zero. Profile `1` is always set. This firmware sends `0x0F`. |

| Bit | Capability |
| ---: | --- |
| 0 | Configuration save |
| 1 | Clock profile selection |
| 2 | Save and reboot |
| 3 | LED control/readback |
| 4 | Standalone reboot |
| 5 | Firmware memory report |
| 6 | Runtime status report |
| 7 | Pin assignment report |

Hosts must not send an operation or read an optional report unless its
capability is advertised. Without bit 1, `clock_profile_mask` is zero. LED
capability is absent on boards without a default LED.

## Report ID 2: Board Configuration

Feature report, host reads, 21-byte payload.

Running values describe the active configuration; target values describe
pending settings.

| Offset | Size | Field | Meaning |
| -----: | ---: | ----- | ------- |
| 0 | 1 | `version` | Layout version, `1`. |
| 1 | 7 | `running` | Bytes 0-2 Bank A/B/C backend, bytes 3-5 Bank A/B/C role, byte 6 7-bit I2C address. Values use the enums below. |
| 8 | 7 | `target` | Same layout for pending settings. |
| 15 | 1 | `led_state` | Bit 0 identification LED on; bits 1-7 zero. Valid only with identity capability bit 3; older firmware returns zero here. |
| 16 | 1 | `clock_profile` | Target system clock request (profile table below). Always a valid, accepted profile. |
| 17 | 4 | `system_clock_hz` | Running system clock in Hz, separate from the requested profile. |

Bank enums: backend `0` HW, `1` PIO, `2` SW. The backend is fixed per bank
(A is HW, B is PIO, C is SW) and is not stored by the firmware; it is reported
as a constant and a `SET_BANK` with any other value is rejected. Role `0` is
generator and role `1` is monitor.

Report 2 is read-only. Set targets through report 20; save and restart to apply.

### System Clock Profile

CDC sets these profiles with `config set clock=<value>` (`default`, `150`,
`200`, or `250`):

| Profile | Requested `clk_sys` | Notes |
| ------: | ------------------- | ----- |
| `0` | SDK default | No reconfiguration (125 MHz RP2040, 150 MHz RP2350); used when storage is invalid or erased. |
| `1` | 150 MHz | Overclock on RP2040; default on RP2350. |
| `2` | 200 MHz | Overclock. |
| `3` | 250 MHz | Overclock. |
| `4`-`255` | Unassigned | Invalid. |

Rules:

- `clock_profile` in report 2 is the target profile, including unsaved changes made by `SET_CLOCK_PROFILE`.
  This firmware sets `clock_profile_mask` to `0x0F` and capability bit 1.
- The profile is a request, not the running clock. Hosts read `clock_profile` and `system_clock_hz` separately
  and never infer one from the other. A profile above `3` returns `INVALID` and changes nothing.
- Profiles above `1` are overclocks. `SET_CLOCK_PROFILE` for one, and a `SAVE_CONFIG` while the target profile is
  above `1`, require the unsafe-overclock acknowledgement flag; without it the request returns `INVALID` and changes
  nothing. Other requests must not set the flag. See the
  [clock guide](../guides/configuration.md#clock-profiles) for precautions
  and recovery.

## Report ID 3: Firmware Memory

Feature report, host reads, 9-byte payload; available only with identity capability
bit 5.

| Offset | Size | Field | Meaning |
| ---: | ---: | --- | --- |
| 0 | 1 | `version` | Layout version, `1`. |
| 1 | 4 | `ram_reserved_bytes` | SRAM reserved by the firmware image, scratch data, and core stacks. |
| 5 | 4 | `ram_total_bytes` | Physical SRAM capacity: 264 KiB on Pico, 520 KiB on Pico 2. |

These values do not report live heap or stack use, or free memory.

## Report ID 4: Runtime Status

Feature report, 13 bytes, available with identity capability bit 6:

| Offset | Size | Field | Meaning |
| -----: | ---: | ----- | ------- |
| 0 | 1 | `version` | Layout version, `1`. |
| 1 | 4 | `uptime_seconds` | Unsigned seconds since boot. |
| 5 | 4 | `temperature_millicelsius` | Signed internal die-temperature estimate; `INT32_MIN` means invalid ADC reading. |
| 9 | 2 | `core0_load_bp` | Core 0 service load in basis points (0-10000). |
| 11 | 2 | `core1_load_bp` | Core 1 service load in basis points (0-10000). |

Temperature is an uncalibrated estimate, not ambient temperature. Service load
is measured over an approximately one-second window and starts at zero.

## Report ID 5: Pin Assignment

Feature report, 28 bytes, available only with identity capability bit 7:

| Offset | Size | Field | Meaning |
| -----: | ---: | ----- | ------- |
| 0 | 1 | `version` | Layout version, `1`. |
| 1 | 24 | `channel_gpio[24]` | GPIO numbers for logical channels 0-23, in channel order. |
| 25 | 1 | `i2c_sda_gpio` | I2C SDA GPIO. |
| 26 | 1 | `i2c_scl_gpio` | I2C SCL GPIO. |
| 27 | 1 | `led_gpio` | Board LED GPIO. |

Each GPIO is 0-47, or `255` when unavailable. These are GPIO numbers, not
physical header-pin numbers.

## Reports 10-12: Bank Status

Feature reports, host reads, 41-byte payloads. Report `10 + bank` carries the
eight logical channels of that bank (A: `10`, B: `11`, C: `12`).

- The running role is in report 2.
- A stopped generator reports `0 Hz / 0%`. A monitor reports its latest sample;
  static inputs settle to `0 Hz / 0%` or `0 Hz / 100%` on a backend-dependent
  schedule. An unavailable monitor sample also reports `0 Hz / 0%`, so it
  cannot be distinguished from static low.
- There is no timestamp or validity flag.

| Offset | Size | Field | Meaning |
| -----: | ---: | ----- | ------- |
| 0 | 1 | `version` | Layout version, `1`. |
| 1 | 40 | `channels[8]` | 5 bytes each: `freq_hz` u32, `duty` u8. Entry *n* is logical channel `8 * bank + n`. |

Each entry reports realized frequency and duty. Channels are read separately;
if one cannot be read, the report fails instead of returning partial data.

## Report ID 20: Board Action & Result

SET requests an action; GET reads the latest device-global result.
Both use 12 payload bytes: a HID report ID has one descriptor length.

### SET_REPORT: Request (payload 12 bytes)

| Offset | Size | Field | Meaning |
| -----: | ---: | ----- | ------- |
| 0 | 1 | `version` | Layout version, `1`. |
| 1 | 1 | `transaction_id` | Host-chosen, nonzero, echoed by the result. A zero ID is `INVALID`. |
| 2 | 1 | `operation` | See table below. |
| 3 | 1 | `channel` | Channel for `SET_CHANNEL`, or bank index for `SET_BANK`. |
| 4 | 4 | `arg0` | Operation-specific argument; see the operation table. |
| 8 | 1 | `arg1` | Duty percent for `SET_CHANNEL`; otherwise zero. |
| 9 | 1 | `flags` | Bit 0 confirms save or standalone reboot; bit 1 acknowledges overclock. Other bits zero. |
| 10 | 2 | reserved | Zero. |

Fields are validated before side effects. Invalid fields return `INVALID`
without changing state; unsupported versions/operations return `UNSUPPORTED`.
Unused fields, flag bits, and reserved bytes must be zero.

For `SET_CHANNEL`, `0 Hz` selects a static level. Periodic limits are
10 Hz-10 MHz for hardware PWM, 1 Hz-10 MHz for PIO, and 1 Hz-10 kHz for
software PWM. They are not accuracy guarantees; see
[operating ranges](../guides/configuration.md#pwm-operating-ranges) for
recommended generation and measurement ranges. At `0 Hz`, duty below `100%`
holds the output low and `100%` holds it high. At nonzero frequency, duty
`0%` and `100%` also produce static low and high levels.

| Operation | Arguments and constraints | Behavior |
| --------- | ------------------------- | -------- |
| `1` `SET_CHANNEL` | `channel` is a generator below `channel_count`; `arg0` is `0` or a periodic frequency within the bank's accepted range; `arg1` is duty `[0, 100]`. | Set one generator channel. |
| `2` `STOP` | `channel`, `arg0`, and `arg1` are zero. | Stop every generator output. A failure status can mean the stop was partial. |
| `3` `SET_BANK` | `channel` is bank `0`-`2`; `arg0` byte 0 is backend and byte 1 is role; the pair must be allowed for that bank. | Set the bank's target role. |
| `4` `SET_I2C_ADDRESS` | The low byte of `arg0` is a 7-bit address in `8..119`. | Set the target I2C address. |
| `5` `SAVE_CONFIG` | Flag bit 0 set; also bit 1 for an overclock target. Requires capability bit 0. | Persist target settings without restarting. |
| `6` `SET_CLOCK_PROFILE` | The low byte of `arg0` is set in `clock_profile_mask`; profiles above `1` require flag bit 1. Requires capability bit 1. | Set the target clock profile. |
| `7` `SAVE_REBOOT` | Flag bit 0 set; also bit 1 for an overclock target. Requires capability bit 2. | Save target settings and restart after the host reads the successful result. Storage failure does not restart. |
| `8` `SET_LED` | `arg0` is `0` or `1`. Requires capability bit 3. | Set the identification LED; a board without an LED returns `UNAVAILABLE`. |
| `9` `REBOOT` | Flag bit 0 set. Requires capability bit 4. | Restart without saving target settings. |

### GET_REPORT: Result (payload 12 bytes)

| Offset | Size | Field | Meaning |
| -----: | ---: | ----- | ------- |
| 0 | 1 | `version` | Layout version, `1`. |
| 1 | 1 | `transaction_id` | Echo of the latest accepted request; `0` before any request. |
| 2 | 1 | `operation` | Echo of the latest accepted request; `0` before any request. |
| 3 | 1 | `status` | Result code, described below. |
| 4 | 8 | reserved | Zero. |

| Value | Name | Meaning |
| ---: | --- | --- |
| 0 | `OK` | Completed successfully. |
| 1 | `BUSY` | Control-API reentry rejected the PWM operation; it is not queued. |
| 2 | `INVALID` | Request fields or operation are invalid. |
| 3 | `UNAVAILABLE` | Requested operation is unavailable. |
| 4 | `TIMEOUT` | Channel operation was not claimed within 1 second and was cancelled. |
| 5 | `APPLY_FAILED` | Backend failed to apply a channel operation. |
| 6 | `PENDING` | Accepted request is still processing. |
| 7 | `UNSUPPORTED` | Unsupported version or operation. |
| 8 | `STORAGE_FAILED` | Configuration could not be saved or verified; previous saved settings remain. |

`UNAVAILABLE` may be returned by `SET_CHANNEL`, `STOP`, or `SET_LED`.
`TIMEOUT` applies only to `SET_CHANNEL` and `STOP`. A save without its
confirmation flag returns `INVALID`.

Results carry status only. After success, read bank state or report 2 for settings.

### Transaction Rules

Serialize requests per device:

- **Framing:** SET is exactly 12 payload bytes (13 with report ID). Wrong
  lengths are ignored without changing the result. Transfer success is not
  acceptance: match `transaction_id` and `operation`.
- **Validation:** rejected requests echo the ID and operation.
- **Slot:** initial GET is version 1 with other fields zero. A free slot accepts
  correctly framed SETs, even invalid ones, and reports `PENDING` until final.
  The slot frees at final status; while occupied, SETs are dropped without
  changing the result.
- **IDs and retries:** increment IDs, skipping `0` on wrap. IDs are not
  deduplicated. After drops, lost status, or `BUSY`, read the result before
  retrying with a new ID. Completion requires matching ID and operation.
- **Result lifetime:** GET does not consume results. The next accepted SET
  replaces them; reset/disconnect clears them. Discard pending assumptions
  and reread identity.
- **Concurrent interfaces:** all three interfaces can change channels; only
  CDC/HID change configuration. Last write wins. Reports 10-12 and 2 are
  authoritative readback.

### Conformance Examples

Hex blocks are payloads without the transport report ID. Assume channel 3 is a generator and save is available.

**SET_CHANNEL, channel 3 to 1000 Hz at 50% (SET):**

```text
01 01 01 03 E8 03 00 00 32 00 00 00
```

**Result OK (GET):**

```text
01 01 01 00 00 00 00 00 00 00 00 00
```

**SAVE_CONFIG with confirmation (SET), transaction 2:**

```text
01 02 05 00 00 00 00 00 00 01 00 00
```

**SET_CHANNEL rejected: duty 101 (SET, then GET):**

```text
01 03 01 03 E8 03 00 00 65 00 00 00
01 03 01 02 00 00 00 00 00 00 00 00
```

**Report 2 (illustrative values) with Bank A HW, B PIO, C SW (all generators), I2C `0x20`, target equal to running, clock profile 0, 150 MHz:**

```text
01 00 01 02 00 00 00 20 00 01 02 00 00 00 20 00 00 80 D1 F0 08
```

**Report 10 with channel 0 at 1000 Hz and 50%, others stopped:**

```text
01 E8 03 00 00 32 00 00 00 00 00 00 00 00 00 00
00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
00 00 00 00 00 00 00 00 00
```
