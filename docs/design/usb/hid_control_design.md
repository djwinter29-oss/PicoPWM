# USB HID Control Design

**Status: proposed; not implemented.** PicoPWM currently exposes USB CDC and
I2C only. TinyUSB HID is disabled, and this document does not describe a
shipping interface.

## Purpose

Add a vendor-defined HID interface as an optional management path for
browser-based WebHID tools and other HID-capable clients. Keep the existing
USB CDC shell as the interactive text interface and I2C as the binary
controller interface. HID must not replace either transport or create a
second control model.

This follows the separation used by
[PicoUART](https://github.com/djwinter29-oss/PicoUART): CDC handles its primary
data workflow, while a distinct vendor HID interface supports management and
diagnostics. PicoPWM should reuse that separation, not PicoUART's UART-specific
reports or channel model.

## Architectural Boundaries

- Add HID as another Core 0 transport. It must not call generator or monitor
  backends directly or bypass `pwmdriver/pwm_driver.*`.
- Route channel reads and mutations through `device_api`. Reads report the
  realized `pwmdriver` snapshot; writes enter the same command path as CDC and
  I2C.
- Reuse existing shared semantics: channel IDs, capability checks,
  configuration behavior, and result codes. HID must not maintain a shadow
  channel-state cache.
- Keep USB callbacks short and bounded. Parse and validate a request, then
  defer control work to Core 0 polling. In particular, do not persist
  configuration or wait for Core 1 from a TinyUSB callback.
- Do not change the existing CDC interface behavior, I2C register map, or
  channel semantics when adding HID.

## Initial Scope

The management client should be able to:

- read device identity, firmware version, and channel count
- read running and target bank configuration
- read realized channel state on demand
- request channel frequency/duty changes and `stop`
- update and save supported target configuration, including the system clock
  profile, which still takes effect only after reboot

`pulse_count` retains its existing monotonic-from-power-on meaning. HID
readback is a snapshot, not waveform history. Periodic telemetry and
per-period event streaming are out of scope initially; the host can poll when
it needs a fresh snapshot.

Board LED and reboot operations should be added only if they are exposed
through an appropriate shared `device_api` operation. The HID transport must
not call board internals directly.

## Identity And Collision Avoidance

PicoPWM shares the lab `0xCAFE` vendor ID with its sibling projects, so the
identifiers below are chosen so a host can never confuse the products.

| Identifier | Value | Notes |
| ---------- | ----- | ----- |
| USB VID | `0xCAFE` | Unallocated lab ID shared by sibling projects; replace with an allocated VID/PID before distributing devices. |
| USB PID | `0x4012` | Unique among siblings: PicoUart `0x4010`, PicoForge `0x4011`, PicoLogic `0x4020`, PicoTrace `0x4023`. |
| HID `product_id` | `0x0003` | Follows PicoUart's registry: `0x0001` PicoUart, `0x0002` PicoForge. Register `0x0003` there. |
| HID usage | page `0xFF00`, usage `0x01` | Vendor-defined, same as siblings; WebHID must filter on VID, PID, and usage page. |

PicoPWM currently advertises `0xCAFE:0x4010`, which collides with PicoUart. Moving to `0x4012` is part of the HID implementation, together with
the USB identity check in `tools/release/check-usb-identity.py` and the USB
identity documentation. Hosts must still validate `product_id` from the
identity report before decoding anything, because VID/PID alone are not an
allocated identity.

## Report Model

Use a vendor-defined HID interface with feature reports for request/response
management. One flat status report carries every channel, so a client reads the
whole device in a single request. No input reports are used initially.

Report IDs follow the sibling convention so the three-byte discovery prefix
(`version`, little-endian `product_id`) is shared, but payloads are
PicoPWM-specific. Report IDs are scoped to one device, so identical numbers in
other products do not conflict; the PID and `product_id` do the discriminating.

| ID | Direction | Name | Purpose |
| -: | --------- | ---- | ------- |
| 1 | Feature, read | Identity | Static device identity and capabilities. |
| 2 | Feature, read | Configuration | Running and target startup configuration. |
| 3 | Feature, read | Status | All channels' realized state in one flat report. |
| 20 | Feature, write | Request | One transaction: operation, transaction ID, arguments. |
| 21 | Feature, read | Response | Result for the latest accepted request. |

Multi-byte integers are little-endian, matching the I2C protocol. Payload
sizes exclude the report ID.

### Report 1: Identity (payload 19 bytes)

| Offset | Size | Field | Meaning |
| -----: | ---: | ----- | ------- |
| 0 | 1 | `version` | Layout version, `1`. |
| 1 | 2 | `product_id` | `0x0003` for PicoPWM. |
| 3 | 8 | `device_uid` | SDK board ID, in USB serial-string order. |
| 11 | 1 | `firmware_major` | Firmware version major. |
| 12 | 1 | `firmware_minor` | Firmware version minor. |
| 13 | 1 | `firmware_patch` | Firmware version patch. |
| 14 | 1 | `mcu` | `1` RP2040, `2` RP2350. |
| 15 | 1 | `channel_count` | From `device_api_channel_count()` (24 today). |
| 16 | 1 | `capabilities` | Bit 0 config save, bit 1 reboot, bit 2 LED, bit 3 clock profile; unset bits are unsupported. |
| 17 | 1 | `i2c_address` | Running 7-bit I2C address. |
| 18 | 1 | `clock_profile_mask` | Bit *n* set when clock profile *n* (0-4) is accepted by this firmware build; bits 5-7 zero. |

### Report 2: Configuration (payload 21 bytes)

Backed by `device_api_config_get_running()` and `device_api_config_get_target()`.

| Offset | Size | Field | Meaning |
| -----: | ---: | ----- | ------- |
| 0 | 1 | `version` | Layout version, `1`. |
| 1 | 7 | `running` | Bank A/B/C backend, Bank A/B/C role, I2C address. |
| 8 | 7 | `target` | Same layout; takes effect only after save and reboot. |
| 15 | 1 | `target_dirty` | `1` when the target differs from the saved copy. |
| 16 | 1 | `clock_profile` | Target system clock request (profile table below); `255` only when storage was invalid or erased. Takes effect after save and reboot. |
| 17 | 4 | `system_clock_hz` | Running `clk_sys`, from `system_clock_hz()`; separate from the request. |

The target is changed and saved through report 20, never by writing report 2.

### System Clock Profile

PicoPWM already raises `clk_sys` to 150 MHz at boot. This makes that choice
configurable, reusing PicoUart's profile IDs so hosts share one table:

| Profile | Requested `clk_sys` | Notes |
| ------: | ------------------- | ----- |
| `0` | SDK default | No reconfiguration (125 MHz RP2040, 150 MHz RP2350). |
| `1` | 150 MHz | Current PicoPWM behavior; the default when storage is invalid or erased. |
| `2` | 200 MHz | Overclock. |
| `3` | 250 MHz | Overclock. |
| `4` | 280 MHz | Overclock; may not be reliable on every board. |
| `5`-`254` | Unassigned | Invalid. |
| `255` | Readback sentinel | Never a valid request. |

Rules:

- The profile is a request, not the running clock. Hosts display
  `clock_profile` and `system_clock_hz` separately and never infer one from
  the other. Firmware may select the highest eligible frequency not above the
  request, or fall back to profile `1`, when the request is unsupported.
- The clock is applied by `system_init_clock()` before any PWM backend
  caches `clk_sys`, so it can only change at boot. HID never reclocks a
  running device; backend timing, frequency limits, and realized frequencies
  are derived from the clock actually applied.
- Profiles above `1` are overclocks. `SET_CLOCK_PROFILE` and a `SAVE_CONFIG`
  that would persist one require the unsafe-overclock acknowledgement flag;
  without it the request returns `INVALID` and changes nothing. Saving is not
  a stability qualification and needs exact-board validation.
- The stored profile lives with the existing configuration sector, so adding
  it needs a configuration layout version bump. Older sectors load with
  profile `1`, and the profile is read before `system_init_clock()`.
- Routed through `device_api` (for example
  `device_api_config_set_clock_profile()`), so CDC and I2C could expose it
  later; HID does not call `board/system.*` directly.

### Report 3: Status (payload 218 bytes)

One flat snapshot of every logical channel, sized for the maximum 24
(`PWM_DRIVER_CHANNEL_COUNT`) so the layout never depends on configuration.
Entry *n* is logical channel *n*; entries at or beyond `channel_count` are
zero. Whether a channel is a generator or a monitor comes from report 2's
running configuration, not from this report.

| Offset | Size | Field | Meaning |
| -----: | ---: | ----- | ------- |
| 0 | 1 | `version` | Layout version, `1`. |
| 1 | 1 | `channel_count` | Valid entries, from `device_api_channel_count()`. |
| 2 | 216 | `channels[24]` | 9 bytes each: `freq_hz` u32, `duty` u8, `pulse_count` u32. |

Each entry is the realized `device_api_get_channel()` value; HID keeps no
shadow cache. Firmware reads the channels back to back on Core 0 in one
polling pass, so entries are individually coherent but not captured at one
instant across channels. `pulse_count` is monotonic from power-on and `stop`
does not clear it, so hosts compute rates from successive reads rather than
treating one read as history. A failed read of any channel fails the whole
report instead of returning partial or zero-filled data. The 218-byte payload
is larger than one full-speed packet, so the feature read spans several
control-transfer data packets; the descriptor declares the exact length.

### Report 20: Request (payload 12 bytes)

| Offset | Size | Field | Meaning |
| -----: | ---: | ----- | ------- |
| 0 | 1 | `version` | Layout version, `1`. |
| 1 | 1 | `transaction_id` | Host-chosen, echoed by the response. |
| 2 | 1 | `operation` | See table below. |
| 3 | 1 | `channel` | Channel for `SET_CHANNEL`, or bank index for `SET_BANK`. |
| 4 | 4 | `arg0` | `freq_hz`, or backend/role bytes, or clock profile. |
| 8 | 1 | `arg1` | Duty percent `[0, 100]` for `SET_CHANNEL`. |
| 9 | 1 | `flags` | Bit 0 confirms save; bit 1 acknowledges unsafe overclock. Bits 2-7 zero. |
| 10 | 2 | reserved | Zero. |

| Op | Name | Maps to |
| -: | ---- | ------- |
| 1 | `SET_CHANNEL` | `device_api_set_channel(channel, freq_hz, duty)`. |
| 2 | `STOP` | `device_api_restore_defaults()`. |
| 3 | `SET_BANK` | `device_api_config_set_bank()`; `arg0` low byte is backend, next is role. |
| 4 | `SET_I2C_ADDRESS` | `device_api_config_set_i2c_address()`. |
| 5 | `SAVE_CONFIG` | `device_api_config_save_target()`; requires flag bit 0, plus bit 1 when the target clock profile is above `1`. |
| 6 | `SET_CLOCK_PROFILE` | Sets the target clock profile from `arg0` low byte; profiles above `1` require flag bit 1. |

`STOP` keeps today's meaning: outputs stop and defaults are restored, while
`pulse_count` stays monotonic from power-on.

### Report 21: Response (payload 8 bytes)

| Offset | Size | Field | Meaning |
| -----: | ---: | ----- | ------- |
| 0 | 1 | `version` | Layout version, `1`. |
| 1 | 1 | `transaction_id` | Echo of the request. |
| 2 | 1 | `operation` | Echo of the request. |
| 3 | 1 | `status` | `0` OK, `1` BUSY, `2` INVALID, `3` UNAVAILABLE, `4` TIMEOUT, `5` APPLY_FAILED, `6` PENDING, `7` UNSUPPORTED. Values 0-5 equal `pwm_driver_result_t`. |
| 4 | 4 | reserved | Zero. |

The response carries only the final command result. After a successful
mutation, read report 3 (or report 2 for configuration) to see the realized
state.

### Transaction Rules

Serialize transactions per device. Writing report 20 with a transaction ID
returns `PENDING` in report 21 until Core 0 finishes the work. A new request
while one is in flight is rejected with `BUSY` and the in-flight response is
not overwritten. Clients must match `transaction_id` and `operation` and must
not treat a stale response as success. Malformed lengths, unsupported
versions, and unknown operations return `UNSUPPORTED` or `INVALID` without
side effects.

Before implementation, freeze the table above in a protocol reference with
shared host/firmware test vectors, including at least one vector per
operation and per error status.

## USB Integration

Add a HID interface to the existing composite USB configuration while keeping
the current CDC interface and its line behavior intact. HID descriptor and
report changes must be tested on both RP2040 and RP2350 builds and with the
target host APIs, including WebHID report-ID handling.

Core 0 should service HID from its normal polling loop alongside CDC and I2C.
The HID request path should validate framing before calling `device_api`;
transport callbacks should only capture or return bounded report data. Device
operations must continue to use the shared control facade and preserve
existing error reporting.

## Implementation Checklist

1. Freeze the report contract and add host/firmware vectors.
   Move the USB PID to `0x4012` and update the USB identity check.
2. Implement the HID transport, descriptors, and bounded request/response
   state.
3. Wire HID polling on Core 0 and route supported operations through
   `device_api`.
4. Add protocol tests for malformed lengths, unsupported versions/operations,
   busy requests, stale transaction IDs, and result propagation.
5. Update user documentation only when the interface is implemented and
   available in firmware.
