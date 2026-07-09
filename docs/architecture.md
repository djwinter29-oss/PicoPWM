# Architecture

PicoPWM targets Raspberry Pi Pico (RP2040) and Pico 2 (RP2350) with one shared logical PWM model and two firmware roles:

- PWM generator
- PWM monitoring with the same external pin order

The current firmware implements the generator role with 24 logical channels:

- 8 hardware PWM channels on PWM slice channel B pins
- 8 PIO PWM channels on companion pins
- 8 software PWM channels for lower-frequency outputs

The hardware bank intentionally uses channel B so the output pinout stays aligned with monitoring-oriented input use cases.

For state machines, API call flows, backend-internal behavior, and publication/readback details, see [detail/pwm_driver_design.md](detail/pwm_driver_design.md).

## Dual-Core Overview

```
┌─ Core 0 — Communication and User API ──────────────┐
│                                                     │
│  main.c                                             │
│    ├── stdio_init_all()                             │
│    ├── control_init()                               │
│    ├── pwm_driver_launch()                          │
│    ├── wait for pwm_driver_is_ready()               │
│    ├── cdc_cmd_init()                               │
│    ├── i2c_slave_init()                             │
│    └── main loop: cdc_cmd_poll() + i2c_slave_poll() │
│                                                     │
│  control.c                                          │
│    ├── validate requested freq/duty                 │
│    ├── submit pwm_driver_set_freq() command         │
│    └── read pwm_driver_get() snapshot               │
│                                                     │
│  cmd_parser.c / cdc_cmd.c / i2c_slave.c             │
└─────────────────────────────────────────────────────┘
                 │
                 │ mailbox command
                 ▼
┌─ Core 1 — PWM Backend Ownership ───────────────────┐
│                                                     │
│  pwmdriver/pwm_driver.c                             │
│    ├── hw_pwm_driver_init()                         │
│    ├── pio_pwm_driver_init()                        │
│    ├── sw_pwm_driver_init()                         │
│    └── main loop: process mailbox + __wfe()         │
│                                                     │
│  pwmdriver/hw_pwm_driver.c                          │
│    ├── owns PWM slices                              │
│    └── wrap IRQ updates pulse counters              │
│                                                     │
│  pwmdriver/pio_pwm_driver.c                         │
│    ├── owns PIO programs and state machines         │
│    └── PIO IRQ updates pulse counters               │
│                                                     │
│  pwmdriver/sw_pwm_driver.c                          │
│    ├── owns 10 us repeating timer                   │
│    └── timer callback updates pulse counters        │
└─────────────────────────────────────────────────────┘
```

## Core Boundary

The `pwmdriver` layer is the Core 1 ownership boundary.

- Core 0 never calls backend driver functions directly.
- Core 0 sends command-path `set_freq(freq, duty)` requests through `pwm_driver_set_freq()`.
- Core 1 applies the request to the selected backend.
- Core 1 publishes the realized channel state into a shared snapshot.
- Core 0 reads that snapshot through `pwm_driver_get()`.

This keeps hardware state, IRQ handlers, and backend-specific timing entirely on Core 1 while still presenting a single user-facing API.

`pwm_driver_set_freq()` is not intended to be a general-purpose internal service call. It is reserved for top-level command ingress paths such as the CDC CLI and, if write support is added later, an I2C command handler.

## Mailbox Model

`pwm_driver_set_freq()` is the only cross-core mutation path.

- Core 0 validates and submits logical channel updates.
- Core 1 dequeues and applies them to the selected backend.
- Core 1 publishes realized state for later reads.
- Submission success means the command entered the mailbox; realized state is observed later through `pwm_driver_get()`.

The architectural model is asynchronous command submission with Core 1 ownership of backend apply timing. The detailed command flow is documented in [detail/pwm_driver_design.md](detail/pwm_driver_design.md).

## Shared State Snapshot

The read path uses a published snapshot owned by `pwmdriver/pwm_driver.c`.

Each logical channel exposes:

- `freq_hz`
- `duty`
- `pulse_count`

Backends publish new realized state after successful configuration changes, and they publish `pulse_count` updates from their IRQ or timer paths. Core 0 reads through `pwm_driver_get()` rather than keeping a separate shadow cache.

The important consequence is that reads reflect realized backend state, not just the last requested value.

## Channel Classes

| Logical Channels | Backend | GPIOs | Notes |
|------------------|---------|-------|-------|
| `0..7` | Hardware PWM | 1, 3, 5, 7, 9, 11, 13, 15 | PWM slices 0..7, channel B |
| `8..15` | PIO PWM | 0, 2, 4, 6, 8, 10, 12, 14 | Companion pins to the hardware bank |
| `16..23` | Software PWM | 18, 19, 20, 21, 22, 25, 26, 27 | Lower-frequency bank |

## Backend Roles

- Hardware PWM: highest accuracy, independent frequency per channel, about 10 Hz to 1 MHz.
- PIO PWM: mid-range timing option, about 10 Hz to 100 kHz.
- Software PWM: low-frequency bank, about 1 Hz to 1 kHz.

Backend implementation detail is intentionally kept out of this overview page. See [detail/pwm_driver_design.md](detail/pwm_driver_design.md) for backend-specific design.

## Transport Model

### USB CDC

- Read/write command interface
- Text commands handled on Core 0
- Calls into `control.c`

### I2C Slave

- Read-only status interface
- Address `0x40` on GPIO 16/17
- Interrupt-driven handler on Core 0
- Returns device info, version, channel count, and per-channel state

If a future write-capable I2C command interface is added, it should use the same `control.c` to `pwm_driver_set_freq()` command path as the CDC CLI.

## Pulse Counter Semantics

`pulse_count` is read-only and monotonic from power-on.

- It increments once per generated PWM period.
- It wraps naturally on 32-bit overflow.
- `stop` disables output by setting frequency to `0 Hz` and duty to `50%`.
- `stop` does not clear `pulse_count`.

## Startup Flow

1. Core 0 optionally raises system clock to 150 MHz.
2. Core 0 initializes stdio.
3. Core 0 initializes the control layer.
4. Core 0 launches Core 1 with `pwm_driver_launch()`.
5. Core 1 initializes hardware PWM, PIO PWM, and software PWM backends.
6. Core 1 sets the ready flag and enters the mailbox loop.
7. Core 0 waits for `pwm_driver_is_ready()`.
8. Core 0 starts CDC and I2C interfaces.

## Design Notes

- Core 1 owns all backend driver state and all PWM-related IRQ/timer activity.
- Core 0 remains dedicated to CLI and I2C responsiveness.
- The `pwmdriver` wrapper exists to hide backend differences and enforce the multicore ownership boundary.
- The single source of truth for channel state is the `pwm_driver` snapshot, not a second cache in `control.c`.
- `pwm_driver_set_freq()` is intended for external command ingress only, not arbitrary internal call sites.
