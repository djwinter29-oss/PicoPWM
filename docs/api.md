# Internal API

This page documents the current source-level API in `firmware/src/`.

The present design centers on `pwmdriver/pwm_driver.*` as the multicore ownership boundary:

- Core 0 uses `control.c` and the transport layers.
- Core 1 owns all backend driver state and timing engines.
- `pwm_driver_set_freq()` crosses the core boundary.
- `pwm_driver_get()` returns the latest published state snapshot.

`pwm_driver_set_freq()` is intended to be called only from top-level command ingress paths, not as a general internal API. In the current firmware that means the CDC CLI path; if write-capable I2C commands are added later, they should use the same boundary.

For state machines, command sequences, backend timing design, and detailed multicore behavior, see [detail/pwm_driver_design.md](detail/pwm_driver_design.md).

---

## `control.h` — Unified Logical Control Layer

`control.h` exposes the firmware's user-facing logical channel operations.

### Constants

| Constant | Value | Description |
|----------|-------|-------------|
| `CONTROL_CHANNEL_COUNT` | 24 | Total logical PWM channels |

### Channel Map

| Logical Range | Backend |
|---------------|---------|
| `0..7` | Hardware PWM |
| `8..15` | PIO PWM |
| `16..23` | Software PWM |

### Functions

#### `void control_init(void)`

Initialize the control layer before launching Core 1. The current implementation does not maintain a second shadow state cache.

#### `bool control_set_freq(uint channel, float freq_hz)`

Validate the requested frequency for the logical channel and forward the update through `pwm_driver_set_freq()`. This is the command-ingress layer above the PWM driver mailbox.

#### `bool control_set_duty(uint channel, float duty)`

Clamp and validate `duty` in the range `0.0 .. 1.0`, then forward the update through `pwm_driver_set_freq()` using the channel's current realized frequency.

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

#### `void control_stop_all(void)`

Request `freq = 0` and `duty = 0.5` for every logical channel. `pulse_count` is not reset.

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

#### `bool pwm_driver_set_freq(uint channel, float freq_hz, float duty)`

Command-path mutation API.

- Intended callers are top-level command ingress paths such as the CDC CLI
- If an I2C write command path is added later, it should use this same API
- Architectural intent: submit mailbox work to Core 1 asynchronously
- Core 1 owns the actual backend apply step

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

- `*_driver_init()`
- `*_driver_set_freq(channel, freq_hz, duty)`
- `*_driver_get(channel, &state)`
- `pwm_driver_store_applied_state(...)`
- `pwm_driver_store_pulse_count(...)`

Higher layers should not depend on these headers directly. Detailed backend behavior is documented in [detail/pwm_driver_design.md](detail/pwm_driver_design.md).

---

## `cmd_parser.h` / `cdc_cmd.h` / `i2c_slave.h`

These modules are thin transport or parsing layers around `control.c`.

- `cmd_parser` implements the CLI syntax and formatted status output.
- `cdc_cmd` handles TinyUSB CDC polling.
- `i2c_slave` exposes a read-only binary status protocol on address `0x40`.

At present, only the CDC path issues write commands. The I2C path is status-read-only.

---

## Concurrency Summary

| Data or Action | Owner | Access Pattern | Protection |
|----------------|-------|----------------|------------|
| Backend driver state | Core 1 | Direct backend access only on Core 1 | Core ownership |
| Mailbox commands | Core 0 writer, Core 1 reader | `queue_t` | Pico queue synchronization |
| Published channel snapshot | Core 1 writer, Core 0 reader | Lock-free snapshot read | versioned publish scheme |
| SW PWM channel timing fields | Core 1 apply path + timer callback | Shared within one backend | interrupt masking |

The design intent is that higher layers interact only with `control.c` and `pwm_driver.*`, while backend drivers remain Core 1 implementation details.
