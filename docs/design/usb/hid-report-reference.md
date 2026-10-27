# HID Report Reference

This document is the HID protocol reference for PicoPWM: static device information, board configuration, the
per-bank channel status, and board actions. Version numbers identify individual report layouts, not the scope
or lifetime of this document.

## Report ID Allocation

The ranges are the convention shared with PicoUart and PicoForge; the assignments are PicoPWM-specific.

| IDs | Generic role | PicoPWM assignments |
| --- | ------------ | ------------------- |
| `1`-`9` | Device information and monitoring | `1` Identity, `2` Board Configuration; `3`-`9` unassigned. |
| `10`-`19` | Application monitoring | Three Bank Status reports, addressed as `10 + bank`; `13`-`19` unassigned. |
| `20`-`29` | Device and application control | `20` Board Action & Result; `21`-`29` unassigned. |
| `30`-`255` | Unassigned expansion | None. |

Report IDs are scoped to one device, so identical numbers in other products do not conflict. Reusing the ranges
does not imply shared payload layouts: hosts identify the product and select its contract before decoding
project-specific fields. ID `0` is unused; unassigned IDs require no padding.

PicoPWM exposes one vendor-defined USB HID interface alongside its CDC interface. There are no input reports.

### Identity And Collision Avoidance

PicoPWM shares the lab `0xCAFE` vendor ID with its sibling projects, so the
identifiers below are chosen so a host can never confuse the products.

| Identifier | Value | Notes |
| ---------- | ----- | ----- |
| USB VID | `0xCAFE` | Unallocated lab ID shared by sibling projects; replace with an allocated VID/PID before distributing devices. |
| USB PID | `0x4012` | Unique among siblings: PicoUart `0x4010`, PicoForge `0x4011`, PicoLogic `0x4020`, PicoTrace `0x4023`. |
| HID `product_id` | `0x0003` | Follows PicoUart's registry: `0x0001` PicoUart, `0x0002` PicoForge. Register `0x0003` there. |
| HID usage | page `0xFF00`, usage `0x01` | Vendor-defined, same as siblings; WebHID must filter on VID, PID, and usage page. |

The USB descriptors, `tools/release/check-usb-identity.py`, and the USB
identity documentation must all carry `0xCAFE:0x4012`. Hosts must still
validate `product_id` from the identity report before decoding anything,
because VID/PID alone are not an allocated identity.

## Ownership

| Operation | Interface | Contract |
| --------- | --------- | -------- |
| Interactive shell | USB CDC | Text commands; unchanged by HID. |
| Controller register access | I2C | Binary register map; unchanged by HID. |
| Identity, configuration, channel status | HID reports 1, 2, 10-12 | Read-only; realized state from `device_api`, no HID shadow cache. |
| Channel set and stop | HID report 20 | Same `device_api` path as CDC and I2C. |
| Bank, I2C address, clock profile, save | HID report 20 | Target configuration only; takes effect after save and restart. |

HID does not call `pwmdriver`, backends, or `board/system.*` directly.

## Report IDs

| Report ID | Name | Type | Direction | Payload | Purpose |
| --------- | ---- | ---- | --------- | ------: | ------- |
| `1` | Device Identity & Capabilities | Feature | Host reads | 18 bytes | Identity, firmware version, capabilities. |
| `2` | Board Configuration | Feature | Host reads | 21 bytes | Running and target configuration, clock profile. |
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
| 15 | 1 | `channel_count` | From `device_api_channel_count()`; `24` (three banks of eight). |
| 16 | 1 | `capabilities` | Bit 0 config save, bit 1 clock profile; bits 2-7 reserved and zero. A host treats a cleared bit as the feature being absent: without bit 0 it sends no `SAVE_CONFIG`; without bit 1 it sends no `SET_CLOCK_PROFILE` and ignores `clock_profile` in report 2 (`clock_profile_mask` is then zero). |
| 17 | 1 | `clock_profile_mask` | Bit *n* set when clock profile *n* (0-4) is accepted by this firmware build on this MCU; bits 5-7 zero. Profile `1` is always set. The running I2C address is read from report 2. |

## Report ID 2: Board Configuration

Feature report, host reads, 21-byte payload.

Backed by `device_api_config_get_running()` and `device_api_config_get_target()`.

| Offset | Size | Field | Meaning |
| -----: | ---: | ----- | ------- |
| 0 | 1 | `version` | Layout version, `1`. |
| 1 | 7 | `running` | Bytes 0-2 Bank A/B/C backend, bytes 3-5 Bank A/B/C role, byte 6 7-bit I2C address. Values are the `pwm_driver_config` bank enums below. |
| 8 | 7 | `target` | Same layout; takes effect only after save and restart. |
| 15 | 1 | `target_dirty` | `1` when the target differs from the saved copy. |
| 16 | 1 | `clock_profile` | Target system clock request (profile table below). Always a valid, accepted profile. Takes effect after save and restart. |
| 17 | 4 | `system_clock_hz` | Running `clk_sys`, from `system_clock_hz()`; separate from the request. |

Bank enums, as in `pwm_driver_config.h`: backend `0` HW, `1` PIO, `2` SW (Bank A takes HW or SW, Bank B PIO or SW,
Bank C SW only); role `0` generator, `1` monitor.

The target is changed and saved through report 20, never by writing report 2. A saved target takes effect only after
the device restarts. HID provides no reboot, so the host asks the user to power-cycle or reset the board, then
rereads reports 1 and 2. A host that lost the connection during a save rereads report 2: `target_dirty` of `0` with the
expected `target` means the save completed.

### System Clock Profile

PicoPWM raises `clk_sys` to 150 MHz at boot by default. The choice is
configurable, reusing PicoUart's profile IDs so hosts share one table:

| Profile | Requested `clk_sys` | Notes |
| ------: | ------------------- | ----- |
| `0` | SDK default | No reconfiguration (125 MHz RP2040, 150 MHz RP2350). |
| `1` | 150 MHz | PicoPWM default; used when storage is invalid or erased. |
| `2` | 200 MHz | Overclock. |
| `3` | 250 MHz | Overclock. |
| `4` | 280 MHz | Overclock; may not be reliable on every board. Set in `clock_profile_mask` only where the MCU build accepts it. |
| `5`-`255` | Unassigned | Invalid. |

Rules:

- Profiles `2`-`4` are not qualified for every MCU or board. A profile's `clock_profile_mask` bit is set only for an
  MCU build where it has been validated on hardware; the table is an allocation of IDs, not a guarantee.
- The profile is a request, not the running clock. Hosts display
  `clock_profile` and `system_clock_hz` separately and never infer one from
  the other. A request for a profile absent from `clock_profile_mask` returns
  `INVALID` and changes nothing; a stored profile that is invalid or no longer
  accepted is treated as profile `1` at boot.
- The clock is applied by `system_init_clock()` before any PWM backend
  caches `clk_sys`, so it can only change at boot. HID never reclocks a
  running device; backend timing, frequency limits, and realized frequencies
  are derived from the clock actually applied.
- Profiles above `1` are overclocks. `SET_CLOCK_PROFILE` and a `SAVE_CONFIG`
  that would persist one require the unsafe-overclock acknowledgement flag;
  without it the request returns `INVALID` and changes nothing. Saving is not
  a stability qualification and needs exact-board validation. If a saved profile prevents boot, hold BOOTSEL while
  connecting USB (the ROM bootloader does not depend on the flashed clock) and reflash; erasing flash restores
  profile `1`.
- The stored profile lives in the configuration sector under its own layout
  version. Sectors without the field load as profile `1`, and the profile is
  read before `system_init_clock()`.
- The profile is read and written through a `device_api` clock-profile accessor that the firmware must add alongside
  the other configuration functions; `system_clock_hz()` already provides the running clock. HID does not call
  `board/system.*` directly, and CDC and I2C can expose the same accessor later.

## Reports 10-12: Bank Status

Feature reports, host reads, 41-byte payload. Report `10 + bank` carries the eight logical channels of that bank
(Bank A `10`, Bank B `11`, Bank C `12`), so every report fits 64 bytes including the ID. The bank is addressed by
report ID; there is no selector field. Whether a channel is a generator or a monitor comes from report 2's running
configuration, not from these reports. A `freq_hz` of `0` means a static level: a stopped generator reports duty `0`, and a monitor that saw no transition
for more than one second reports duty `0` or `100` for a static low or high input. A monitor keeps its last
measured values until that timeout, so hosts treat monitor readings as at most about one second old. The reports carry
no timestamp or validity flag; a host that needs freshness compares successive reads. This staleness is accepted.

| Offset | Size | Field | Meaning |
| -----: | ---: | ----- | ------- |
| 0 | 1 | `version` | Layout version, `1`. |
| 1 | 40 | `channels[8]` | 5 bytes each: `freq_hz` u32, `duty` u8. Entry *n* is logical channel `8 * bank + n`. |

Each entry is the realized `freq_hz` and `duty` from `device_api_get_channel()`; HID keeps no shadow cache.
Firmware reads the eight channels back to back on Core 0 in one polling pass, so entries within a report are
individually coherent but not captured at one instant, and the three banks are separate captures. A failed read of
any channel fails the whole report instead of returning partial or zero-filled data. The read must stay bounded and
must not wait on Core 1.

`pulse_count` is deliberately absent: hardware and PIO generators derive it from elapsed time and PIO monitors report
`0`, so it is not a measured count and HID does not publish it. CDC and I2C are unchanged.

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
| 9 | 1 | `flags` | Bit 0 confirms save; bit 1 acknowledges unsafe overclock. Bits 2-7 zero. |
| 10 | 2 | reserved | Zero. |

| Op | Name | Maps to |
| -: | ---- | ------- |
| 1 | `SET_CHANNEL` | `device_api_set_channel(channel, freq_hz, duty)`. |
| 2 | `STOP` | `device_api_restore_defaults()`; outputs stop and defaults are restored. |
| 3 | `SET_BANK` | `device_api_config_set_bank()`; `channel` is the bank (`0`-`2`), `arg0` byte 0 backend, byte 1 role. |
| 4 | `SET_I2C_ADDRESS` | `device_api_config_set_i2c_address()`; `arg0` low byte is the 7-bit address. |
| 5 | `SAVE_CONFIG` | `device_api_config_save_target()`; requires flag bit 0, plus bit 1 when the target clock profile is above `1`. Persists only; it does not restart the device. See Save Cost. |
| 6 | `SET_CLOCK_PROFILE` | Sets the target clock profile from `arg0` low byte; profiles above `1` require flag bit 1. |

Operation validity, checked before any side effect; every failure returns `INVALID` and changes nothing:

- `SET_CHANNEL`: `channel` below `channel_count`, the channel is a generator in the running configuration, `arg1`
  is `[0, 100]`, and `freq_hz` is accepted by `device_api_set_channel()`.
- `STOP`: `channel`, `arg0`, and `arg1` are zero.
- `SET_BANK`: `channel` is `0`-`2`, and the backend/role pair is allowed for that bank.
- `SET_I2C_ADDRESS`: the address is accepted by `device_api_config_set_i2c_address()`.
- `SAVE_CONFIG`: flag bit 0 set (and bit 1 for an overclock target), and no other fields set.
- `SET_CLOCK_PROFILE`: the profile is set in `clock_profile_mask`.
- Unused fields and reserved bits are zero; a nonzero value is `INVALID`.

### Save Cost

`SAVE_CONFIG` erases and programs flash. The SDK flash routines do not pause the other core, so firmware locks out
Core 1 for the erase and program (`multicore_lockout_start_blocking()`). While locked out, Core 1 services nothing:
software-backend edges and monitor sampling are delayed, and the host should expect disturbed software generators and
stale monitor readings for that interval. Hosts should stop software generators and avoid relying on monitors while a
save is `PENDING`. The save runs from Core 0 polling, never from the USB callback. Core 0 is not locked out, so it keeps answering GETs
with `PENDING` until the save finishes.

### GET_REPORT: Result (payload 12 bytes)

| Offset | Size | Field | Meaning |
| -----: | ---: | ----- | ------- |
| 0 | 1 | `version` | Layout version, `1`. |
| 1 | 1 | `transaction_id` | Echo of the latest accepted request; `0` before any request. |
| 2 | 1 | `operation` | Echo of the latest accepted request; `0` before any request. |
| 3 | 1 | `status` | `0` OK, `1` BUSY, `2` INVALID, `3` UNAVAILABLE, `4` TIMEOUT, `5` APPLY_FAILED, `6` PENDING, `7` UNSUPPORTED, `8` STORAGE_FAILED. Values 0-5 equal `pwm_driver_result_t`. `UNAVAILABLE` and `TIMEOUT` can only come from `SET_CHANNEL` and `STOP`, which cross to Core 1; configuration operations never return them. `STORAGE_FAILED` means the configuration write or its verification failed; the previously saved copy is kept. A save without its confirmation flag is `INVALID`. |
| 4 | 8 | reserved | Zero. |

The result carries only the final command status. After a successful mutation, read the bank status reports (or
report 2 for configuration) to see the realized state.

### Transaction Rules

Serialize transactions per device. The rules are:

- **Framing:** TinyUSB acknowledges a Feature SET before the application sees it, and its set-report callback cannot
  stall, so firmware cannot refuse a SET at the USB level. A SET whose length is not 12 is dropped silently with no
  state change. Hosts therefore confirm acceptance by matching `transaction_id`, not by the transfer succeeding; any
  `sendFeatureReport` error is also "not accepted". Every other failure is reported in the result as described below.
- **Status per fault:** unsupported `version` is `UNSUPPORTED`; unknown `operation` is `UNSUPPORTED`; zero
  `transaction_id`, nonzero reserved or unused fields, and any operation-validity failure are `INVALID`.
  A rejected request still echoes its `transaction_id` and `operation` so the host can match it.
- **Before any request** a GET reads all zero.
- **Accepted requests:** a SET is accepted when it passes framing and the request slot is free. The set-report
  callback only copies the 12 bytes into a single pending-request slot; Core 0 validates and executes from there.
  A SET is accepted exactly when the next GET shows its `transaction_id`, because a dropped SET leaves no trace.
  Acceptance replaces the previous result immediately: the GET returns `PENDING` until Core 0 finishes, then the final
  status. A request that fails validation is still accepted and finishes as `INVALID` or `UNSUPPORTED`, so only
  accepted requests ever change the result.
- **Slot lifetime:** the slot is busy from acceptance until the result becomes final, not until a host reads it.
  Core 0 publishes the final status and frees the slot together, so a SET sent after a GET shows a final status
  is never dropped for being early.
- **SET while the slot is busy:** the SET is dropped, nothing executes, and the pending result is untouched. Hosts
  never send while a request is `PENDING`. A host that did sees a GET whose `transaction_id` is not its own after a
  final status, which means "not accepted, no side effect". `BUSY` in a result is reserved for `pwm_driver` contention
  on an executed request.
- **Retries:** every retry, after a drop, a lost status, or a `BUSY` result, uses a new `transaction_id`. A repeated ID
  is indistinguishable from the earlier request, so a host never resends an ID it may already have had accepted
  without first reading the result.
- **Transaction IDs:** a host increments the ID per request and skips `0` on wrap. Firmware does not deduplicate, so a
  repeated ID is a new request.
- **Result lifetime:** the result persists until the next accepted SET and is never consumed by a GET. A reset or
  disconnect clears it to all zero, so hosts discard pending-transaction assumptions and reread identity.
- **Stale results:** clients match `transaction_id` and `operation` and never treat a stale result as success.
- **Concurrent interfaces:** CDC, I2C, and HID may all change channels or the target configuration; the last write
  wins, and the realized state in reports 10-12 and 2 is authoritative.

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

**Report 2 (illustrative values) with Bank A HW, B PIO, C SW (all generators), I2C `0x20`, target equal to running, clock profile 1, 150 MHz:**

```text
01 00 01 02 00 00 00 20 00 01 02 00 00 00 20 00 01 80 D1 F0 08
```

**Report 10 with channel 0 at 1000 Hz and 50%, others stopped:**

```text
01 E8 03 00 00 32 00 00 00 00 00 ... (zero-filled to 41 bytes)
```

## Host Polling and Validation

1. Read report 1, check `product_id` is `0x0003`, and validate version and exact length before sending any request.
   Cache identity and `clock_profile_mask` for the connection.
2. Read report 2 once per connection and after every successful configuration action.
3. Poll reports 10-12 for channel state, one read per bank, every 100-1000 ms (Feature reads use the control
   endpoint, so faster polling only competes with action traffic); all three together form the full channel set but are
   not one atomic capture. Hosts that need an event count use CDC or I2C.
4. Submit actions through report 20, then GET report 20 until `status` is not `PENDING` and `transaction_id` and
   `operation` match. Use one controlling host.
5. After a successful mutation, read reports 10-12 (channels) or report 2 (configuration) to see the realized state.

Validate type, ID, exact length, version, reserved fields and bits, and enum values. A read error invalidates the
affected live data only. Disconnect or restart discards caches and pending-transaction assumptions; rediscover
identity and configuration.

## Design Validation

Descriptor, firmware, Python, .NET, WebHID, and tests must agree on this contract. Writes to read-only reports must
not execute actions or change state. The examples above are part of the shared host/firmware vectors, which cover at least one request per operation
and per status.

Required coverage:

- Wrong-length SET is dropped with no state change and its `transaction_id` never appears in the result; bad version, unknown operation, zero `transaction_id`, and nonzero
  reserved or unused fields return the status listed in Transaction Rules.
- SET while the slot is busy is dropped and leaves the pending result intact; a SET right after a final status is
  accepted; a repeated `transaction_id` runs as a new request.
- Per-operation validity failures return `INVALID` with no side effect.
- Result clearing to zero after reset, and stale-result matching by `transaction_id` and `operation`.
- `SAVE_CONFIG` stays `PENDING` while Core 0 answers GETs, then reports `OK` or `STORAGE_FAILED`.
- Reports 10-12 with every channel populated, and failure of the whole report when any channel read fails.
- Clock profile gating: unaccepted profiles, and overclock without the acknowledgement flag.
- Identity checks: VID, PID, usage page, and `product_id`.
