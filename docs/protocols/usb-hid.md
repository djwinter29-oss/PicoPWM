# USB HID Reports

This document defines PicoPWM's USB HID feature reports.

## Report ID Allocation

Report IDs are allocated by range:

| IDs | Generic role | PicoPWM assignments |
| --- | ------------ | ------------------- |
| `1`-`9` | Device information and monitoring | `1` Identity, `2` Board Configuration, `3` Firmware Memory, `4` Runtime Status, `5` Pin Assignment; `6`-`9` unassigned. |
| `10`-`19` | Application monitoring | Three Bank Status reports, addressed as `10 + bank`; `13`-`19` unassigned. |
| `20`-`29` | Device and application control | `20` Board Action & Result; `21`-`29` unassigned. |
| `30`-`255` | Unassigned expansion | None. |

Report IDs are scoped to one device, so identical numbers in other products do not conflict. Reusing the ranges
does not imply shared payload layouts: hosts identify the product and select its contract before decoding
project-specific fields. ID `0` is unused; unassigned IDs require no padding.

PicoPWM exposes one vendor-defined USB HID interface alongside its CDC interface. There are no input reports.

### Identity and Collision Avoidance

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

Hosts must validate `product_id` from the identity report before decoding
other reports. The HID `product_id` is a protocol identifier, not the USB PID.

## Report IDs

| Report ID | Name | Type | Direction | Payload | Purpose |
| --------- | ---- | ---- | --------- | ------: | ------- |
| `1` | Device Identity & Capabilities | Feature | Host reads | 18 bytes | Identity, firmware version, capabilities. |
| `2` | Board Configuration | Feature | Host reads | 21 bytes | Running and target configuration, clock profile. |
| `3` | Firmware Memory | Feature | Host reads | 9 bytes | Static SRAM reservation and physical capacity. |
| `4` | Runtime Status | Feature | Host reads | 13 bytes | Uptime, internal temperature estimate, service load. |
| `5` | Pin Assignment | Feature | Host reads | 28 bytes | 24 channel GPIOs, I2C SDA/SCL, board LED GPIO. |
| `10`-`12` | Bank Status — Bank A–C | Feature | Host reads | 41 bytes | Realized frequency and duty of the bank's 8 channels. |
| `20` | Board Action & Result | Feature | Host reads/writes | 12 bytes | SET submits one action; GET returns the latest result in the same length. |

Names are documentation and host-display labels, not wire fields.

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
settings that will apply after save and restart.

| Offset | Size | Field | Meaning |
| -----: | ---: | ----- | ------- |
| 0 | 1 | `version` | Layout version, `1`. |
| 1 | 7 | `running` | Bytes 0-2 Bank A/B/C backend, bytes 3-5 Bank A/B/C role, byte 6 7-bit I2C address. Values use the enums below. |
| 8 | 7 | `target` | Same layout; takes effect only after save and restart. |
| 15 | 1 | `led_state` | Bit 0 identification LED on; bits 1-7 zero. Valid only with identity capability bit 3; older firmware returns zero here. |
| 16 | 1 | `clock_profile` | Target system clock request (profile table below). Always a valid, accepted profile. Takes effect after save and restart. |
| 17 | 4 | `system_clock_hz` | Running system clock in Hz, separate from the requested profile. |

Bank enums: backend `0` HW, `1` PIO, `2` SW. The backend is fixed per bank
(A is HW, B is PIO, C is SW) and is not stored by the firmware; it is reported
as a constant and a `SET_BANK` with any other value is rejected. Role `0` is
generator and role `1` is monitor.

Change target settings through report 20, not report 2. They take effect after
the device restarts.

### System Clock Profile

CDC uses the same clock profiles through `config set clock=<value>`:

| Profile | Requested `clk_sys` | Notes |
| ------: | ------------------- | ----- |
| `0` | SDK default | No reconfiguration (125 MHz RP2040, 150 MHz RP2350); used when storage is invalid or erased. |
| `1` | 150 MHz | |
| `2` | 200 MHz | Overclock. |
| `3` | 250 MHz | Overclock. |
| `4`-`255` | Unassigned | Invalid. |

Rules:

- `clock_profile` in report 2 is the target profile, including unsaved changes made by
  `SET_CLOCK_PROFILE` or the CDC command. This firmware sets `clock_profile_mask` to `0x0F` and capability bit 1.
- The profile is a request, not the running clock. Hosts display `clock_profile` and `system_clock_hz` separately
  and never infer one from the other. A profile above `3` returns `INVALID` and changes nothing.
- The clock changes only after save and restart.
- Profiles above `1` are overclocks. `SET_CLOCK_PROFILE` for one, and a `SAVE_CONFIG` while the target profile is
  above `1`, require the unsafe-overclock acknowledgement flag; without it the request returns `INVALID` and changes
  nothing. Other requests must not set the flag. See the
  [configuration guide](../guides/configuration.md) for clock-profile precautions
  and recovery.

## Report ID 3: Firmware Memory

Feature report, host reads, 9-byte payload; available only with identity capability
bit 5.

| Offset | Size | Field | Meaning |
| ---: | ---: | --- | --- |
| 0 | 1 | `version` | Layout version, `1`. |
| 1 | 4 | `ram_reserved_bytes` | SRAM reserved by the firmware image, scratch data, and core stacks. |
| 5 | 4 | `ram_total_bytes` | Physical SRAM capacity: 264 KiB on Pico, 520 KiB on Pico 2. |

These values do not report live heap/stack use or free memory.

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
  after more than one second without a transition it reports static low or
  high as `0 Hz / 0%` or `0 Hz / 100%`.
- There is no timestamp or validity flag.

| Offset | Size | Field | Meaning |
| -----: | ---: | ----- | ------- |
| 0 | 1 | `version` | Layout version, `1`. |
| 1 | 40 | `channels[8]` | 5 bytes each: `freq_hz` u32, `duty` u8. Entry *n* is logical channel `8 * bank + n`. |

Each entry reports realized frequency and duty. Channels are read separately;
if one cannot be read, the report fails instead of returning partial data.

## Report ID 20: Board Action & Result

SET requests one action; GET returns the latest device-global result. Both directions use the same 12-byte payload
length because a report ID has a single descriptor length.

### SET_REPORT: Request (payload 12 bytes)

| Offset | Size | Field | Meaning |
| -----: | ---: | ----- | ------- |
| 0 | 1 | `version` | Layout version, `1`. |
| 1 | 1 | `transaction_id` | Host-chosen, nonzero, echoed by the result. A zero ID is `INVALID`. |
| 2 | 1 | `operation` | See table below. |
| 3 | 1 | `channel` | Channel for `SET_CHANNEL`, or bank index for `SET_BANK`. |
| 4 | 4 | `arg0` | `freq_hz` for `SET_CHANNEL`; for `SET_BANK`, byte 0 backend and byte 1 role (enums under report 2) applied to the bank in `channel`; I2C address for `SET_I2C_ADDRESS`; profile for `SET_CLOCK_PROFILE`. Unused bytes zero. |
| 8 | 1 | `arg1` | Duty percent `[0, 100]` for `SET_CHANNEL`. |
| 9 | 1 | `flags` | Bit 0 confirms save or standalone reboot; bit 1 acknowledges overclock. Bits 2-7 zero. |
| 10 | 2 | reserved | Zero. |

| Op | Name | Behavior |
| -: | ---- | ------- |
| 1 | `SET_CHANNEL` | Set one generator channel using its logical channel, frequency, and duty. |
| 2 | `STOP` | Stop every generator output. A failure status can mean the stop was partial. |
| 3 | `SET_BANK` | Set a bank's target role; `channel` is the bank (`0`-`2`), `arg0` byte 0 backend, byte 1 role. |
| 4 | `SET_I2C_ADDRESS` | Set the target 7-bit I2C address from the low byte of `arg0`. |
| 5 | `SAVE_CONFIG` | Persist target settings; does not restart the device. Requires flag bit 0, plus bit 1 for an overclock target. |
| 6 | `SET_CLOCK_PROFILE` | Set the target clock profile from the low byte of `arg0`; profiles above `1` require flag bit 1. |
| 7 | `SAVE_REBOOT` | Save target settings and restart after the host reads the successful result. Requires capability bit 2. Storage failure does not restart. |
| 8 | `SET_LED` | Set the identification LED using `arg0` (`0` off, `1` on). Requires capability bit 3; a board without an LED returns `UNAVAILABLE`. |
| 9 | `REBOOT` | Restart without saving target settings. Requires capability bit 4 and explicit confirmation (flag bit 0). |

Operation validity, checked before any side effect; every failure returns `INVALID` and changes nothing:

- `SET_CHANNEL`: `channel` below `channel_count`, the channel is a generator in the running configuration, `arg1`
  is `[0, 100]`, and `freq_hz` is within the channel's accepted range.
- `STOP`: `channel`, `arg0`, and `arg1` are zero.
- `SET_BANK`: `channel` is `0`-`2`, and the backend/role pair is allowed for that bank.
- `SET_I2C_ADDRESS`: the address is in `8..119`.
- `SAVE_CONFIG`, `SAVE_REBOOT`: flag bit 0 set (and bit 1 for an overclock target), and no other fields set.
- `SET_CLOCK_PROFILE`: the profile is set in `clock_profile_mask`.
- `SET_LED`: `arg0` is 0 or 1; all other fields zero.
- `REBOOT`: flag bit 0 set, all other fields zero; never persists target changes.
- Unused fields and reserved bits are zero; a nonzero value is `INVALID`.

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
| 1 | `BUSY` | Temporarily waiting to apply an accepted action. |
| 2 | `INVALID` | Request fields or operation are invalid. |
| 3 | `UNAVAILABLE` | Channel operation is unavailable. |
| 4 | `TIMEOUT` | Channel operation was not claimed within 1 second and was cancelled. |
| 5 | `APPLY_FAILED` | Backend failed to apply a channel operation. |
| 6 | `PENDING` | Accepted request is still processing. |
| 7 | `UNSUPPORTED` | Unsupported version or operation. |
| 8 | `STORAGE_FAILED` | Configuration could not be saved or verified; previous saved settings remain. |

`UNAVAILABLE` and `TIMEOUT` apply only to `SET_CHANNEL` and `STOP`. A save
without its confirmation flag returns `INVALID`.

The result carries only the final command status. After a successful mutation, read the bank status reports (or
report 2 for configuration) to see the realized state.

### Transaction Rules

Serialize requests per device:

- **Framing:** A SET contains exactly 12 payload bytes (13 with the report ID). A wrong-length request is ignored
  without changing the result. Hosts confirm acceptance by matching `transaction_id` and `operation`, not by the
  transfer completing successfully.
- **Validation:** unsupported versions or operations return `UNSUPPORTED`; zero transaction IDs, nonzero reserved
  or unused fields, and invalid operation fields return `INVALID`. Rejected requests echo the ID and operation.
- **Slot:** before the first request, GET returns version 1 with all other fields zero. A correctly framed SET is
  accepted when the slot is free, even if validation fails. Its result is `PENDING` until final. A SET while the slot
  is occupied is dropped; it does not change the result. The slot stays occupied until that result is final.
- **IDs and retries:** increment the ID for each request and skip `0` on wrap. Firmware does not deduplicate IDs.
  After a drop, lost status, or `BUSY`, read the result before retrying and use a new ID. Confirm completion only
  when both ID and operation match.
- **Result lifetime:** GET does not consume the result; it remains until the next accepted SET. A reset or disconnect
  clears it, so discard pending assumptions and read identity again.
- **Concurrent interfaces:** CDC, I2C, and HID may all change channels; CDC and HID may also change the target
  configuration. The last write wins, and the realized state in reports 10-12 and 2 is authoritative. The
  documented I2C contract does not include configuration writes.

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

## Host Polling and Validation

1. Read report 1 and validate product ID, version, and exact length before sending requests. Cache identity and
   `clock_profile_mask` for the connection.
2. Read report 2 to inspect the running and target configuration.
3. Poll reports 10-12 for channel state as needed. Each bank is a separate read; the three reports do not form one
   atomic capture.
4. Submit actions through report 20, then GET report 20 until `status` is not `PENDING` and `transaction_id` and
   `operation` match. Use one controlling host.
5. After a successful mutation, reread the affected channel or configuration report.

Request the full declared report length; a shorter report is invalid. Saving
can pause channel service and host communication; see the
[configuration guide](../guides/configuration.md).

Validate type, ID, exact length, version, reserved fields and bits, and enum values. A read error invalidates the
affected live data only. Disconnect or restart discards caches and pending-transaction assumptions; rediscover
identity and configuration.
