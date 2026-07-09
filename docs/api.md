# Internal API

This page documents the current source-level API in `firmware/src/`.

The present design centers on `pwmdriver/pwm_driver.*` as the multicore ownership boundary:

- Core 0 uses the high-level helper APIs in `pwmdriver/pwm_driver.c` and the transport layers.
- Core 1 owns all backend driver state and timing engines.
- `pwm_driver_set_freq()` crosses the core boundary.
- `pwm_driver_get()` returns the latest published state snapshot.

`pwm_driver_set_freq()` is intended to be called only from top-level command ingress paths, not as a general internal API. In the current firmware that means the CDC CLI path; if write-capable I2C commands are added later, they should use the same boundary.

For state machines, command sequences, backend timing design, and detailed multicore behavior, see [detail/pwm_driver_design.md](detail/pwm_driver_design.md).

---

## High-Level Logical Helpers In `pwmdriver/pwm_driver.h`

`pwm_driver.h` now exposes the firmware's user-facing logical channel helpers directly; there is no separate `control.h` layer.

### Constants

| Constant | Value | Description |
|----------|-------|-------------|
| `PWM_DRIVER_CHANNEL_COUNT` | 24 | Total logical PWM channels |

### Channel Map

| Logical Range | Backend |
|---------------|---------|
| `0..7` | Hardware PWM |
| `8..15` | PIO PWM |
| `16..23` | Software PWM |

### Functions

#### `pwm_driver_result_t control_set_freq(uint channel, float freq_hz)`

Validate the requested frequency for the logical channel, read the latest realized duty, and forward the combined update through `pwm_driver_set_freq()`. The read-modify-write helper is serialized on Core 0 so another public write entry point cannot slip between its snapshot read and submit step.

#### `pwm_driver_result_t control_set_duty(uint channel, float duty)`

Clamp and validate `duty` in the range `0.0 .. 1.0`, then forward the update through `pwm_driver_set_freq()` using the channel's current realized frequency. This read-modify-write helper is serialized on Core 0 so another public write entry point cannot slip between its snapshot read and submit step.

#### `pwm_driver_result_t control_set(uint channel, float freq_hz, float duty)`

Validate one full logical channel update and forward one synchronous apply request through `pwm_driver_set_freq()`. This path participates in the same Core 0 write serialization used by the read-modify-write helpers.

#### `bool control_get(uint channel, pwm_driver_state_t *state)`

Return one coherent realized-state snapshot for the logical channel. Higher transport layers should prefer this API when they need more than one field from the same channel state.

#### `float control_get_freq(uint channel)`

Return the latest realized frequency from the `pwm_driver` snapshot. This is a convenience wrapper around `control_get()`.

#### `float control_get_duty(uint channel)`

Return the latest realized duty cycle from the `pwm_driver` snapshot. This is a convenience wrapper around `control_get()`.

#### `uint32_t control_get_pulse_count(uint channel)`

Return the read-only pulse counter from the `pwm_driver` snapshot. This is a convenience wrapper around `control_get()`.

#### `bool control_is_enabled(uint channel)`

Return `true` when the realized frequency is greater than `0`.

#### `pwm_driver_result_t control_stop_all(void)`

Request `freq = 0` and `duty = 0.5` for every logical channel. `pulse_count` is not reset. The stop sweep is serialized on Core 0 so no other public write entry point can interleave partway through the loop. Returns the first non-`OK` write result if any channel stop request fails.

---

## `pwmdriver/pwm_driver.h` — Multicore PWM Wrapper

`pwm_driver.h` is the single wrapper visible to higher command layers.

### Constants

| Constant | Value | Description |
|----------|-------|-------------|
| `HW_PWM_COUNT` | 8 | Hardware PWM logical channel count |
| `PIO_PWM_DRIVER_COUNT` | 8 | PIO PWM logical channel count |
| `SW_PWM_COUNT` | 8 | Software PWM logical channel count |
| `PWM_DRIVER_CHANNEL_COUNT` | 24 | Total logical channels |
| `HW_PWM_CHANNEL_BASE` | 0 | Hardware logical base |
| `PIO_PWM_CHANNEL_BASE` | 8 | PIO logical base |
| `SW_PWM_CHANNEL_BASE` | 16 | Software logical base |

### Types

```c
typedef struct {
    float freq_hz;
    float duty;
    uint32_t pulse_count;
} pwm_driver_state_t;
```

`pwm_driver_state_t` is the shared logical view of one channel's realized state.

### Functions

#### `void pwm_driver_launch(void)`

Launch Core 1, initialize all three PWM backends, and start the mailbox loop.

#### `bool pwm_driver_is_ready(void)`

Return `true` once Core 1 has completed backend initialization.

#### `pwm_driver_result_t pwm_driver_set_freq(uint channel, float freq_hz, float duty)`

Command-path mutation API.

- Intended callers are top-level command ingress paths such as the CDC CLI
- If an I2C write command path is added later, it should defer out of the ISR and use the high-level `control_*()` helpers from the Core 0 command path
- Higher command layers should prefer `control_set()` for full-state writes instead of calling `pwm_driver_set_freq()` directly
- Core 1 callers must not use this API; it returns `PWM_DRIVER_RESULT_UNAVAILABLE` outside the Core 0 command path
- Public write callers first pass through the shared Core 0 serialization lock before they compete for mailbox admission
- Core 0 blocks until Core 1 finishes the backend apply step
- The call returns `PWM_DRIVER_RESULT_BUSY` if the caller reaches the mailbox while another command is already pending or executing on Core 1
- The call returns `PWM_DRIVER_RESULT_INVALID` for invalid channel or non-finite input
- The call returns `PWM_DRIVER_RESULT_UNAVAILABLE` if Core 1 is not ready yet
- The call returns `PWM_DRIVER_RESULT_TIMEOUT` if Core 1 does not publish a reply before the apply timeout; in that case the final hardware outcome is unknown and callers should read back state before retrying
- The call returns `PWM_DRIVER_RESULT_APPLY_FAILED` if Core 1 accepts the command but the backend rejects it

#### `bool pwm_driver_get(uint channel, pwm_driver_state_t *state)`

Return the latest published realized state for a logical channel. Core 0 reads the shared snapshot; Core 1 may read backend state directly.

---

## Backend Driver Headers

The backend headers:

- `pwmdriver/hw_pwm_driver.h`
- `pwmdriver/pwm_driver_internal.h`
- `pwmdriver/pio_pwm_driver.h`
- `pwmdriver/sw_pwm_driver.h`

are Core 1 implementation details.

They each expose the same minimal shape:

- internal pin allocation arrays
- `*_driver_init()`
- `*_driver_set_freq(channel, freq_hz, duty)`
- `*_driver_get(channel, &state)`
- `pwm_driver_store_applied_state(...)`
- `pwm_driver_store_pulse_count(...)`

Higher layers should not depend on these headers directly. Detailed backend behavior is documented in [detail/pwm_driver_design.md](detail/pwm_driver_design.md).

---

## `cmd_parser.h` / `cdc_cmd.h` / `i2c_slave.h`

These modules are thin transport or parsing layers around the high-level helpers in `pwmdriver/pwm_driver.c`.

- `cmd_parser` implements the CLI syntax and formatted status output.
- `cdc_cmd` handles TinyUSB CDC polling.
- `i2c_slave` exposes a read-only binary status protocol on address `0x40`.

At present, only the CDC path issues write commands. The I2C path is status-read-only.

---

## Concurrency Summary

| Data or Action | Owner | Access Pattern | Protection |
|----------------|-------|----------------|------------|
| Backend driver state | Core 1 | Direct backend access only on Core 1 | Core ownership |
| Mailbox command slot | Core 0 writer, Core 1 reader | Single in-flight record | critical section |
| Mailbox reply record | Core 1 writer, Core 0 reader | Shared struct | critical section |
| Published channel snapshot | Core 1 writer, Core 0 reader | Lock-free snapshot read | versioned publish scheme |
| SW PWM channel timing fields | Core 1 apply path + timer callback | Shared within one backend | interrupt masking |

The design intent is that higher layers interact only with `pwmdriver/pwm_driver.*`, while backend drivers remain Core 1 implementation details.
