# Architecture

This page describes the current PicoPWM runtime structure. It focuses on module ownership and request flow, not backend implementation detail.

For mailbox state machines, publication internals, and backend-specific behavior, see [detail/pwm_driver_design.md](detail/pwm_driver_design.md).

## Architecture Documents

Use the architecture-related pages as follows:

- [Architecture](architecture.md) — system structure, layer boundaries, and request flow
- [Firmware Interfaces](firmware_interfaces.md) — source-level interface reference for the current modules
- [PWM Driver Design](detail/pwm_driver_design.md) — detailed `pwmdriver` and backend internals
- [Hardware PWM Design](detail/hw_pwm_design.md) — hardware generator and monitor design, limits, and target range
- [Software PWM Design](detail/sw_pwm_design.md) — software generator and monitor design, limits, and standalone monitor role

## System Model

PicoPWM targets Raspberry Pi Pico (RP2040) and Pico 2 (RP2350) with one shared logical PWM model.

The current firmware image implements the generator role with 24 logical channels:

- `0..7` hardware PWM
- `8..15` PIO PWM
- `16..23` software PWM

Each logical channel exposes the same readback model:

- `freq_hz`
- `duty`
- `pulse_count`

The hardware PWM bank intentionally uses PWM slice channel B pins so the external pin order stays aligned with the monitoring-oriented wiring plan.

## Runtime Layers

The current firmware is split into four practical layers.

### 1. Transport Layer

Core 0 owns the host-facing transports:

- `usb/usb_cdc.*` for TinyUSB CDC byte transport
- `cli/cli_shell.*` for line-oriented command parsing
- `cli/device_cli.*` for human-readable CLI commands
- `i2c/i2c_slave.*` for the I2C slave ISR and deferred write scheduling
- `i2c/i2c_control_map.*` for the I2C register map and payload translation

### 2. Shared Control Layer

`control/control_iface.*` is the transport-neutral Core 0 API shared by USB CDC and I2C.

Responsibilities:

- expose device info and firmware version
- return realized channel state
- translate channel updates into the shared PWM command path
- avoid introducing a second shadow cache above `pwmdriver`

### 3. Multicore Boundary Layer

`pwmdriver/pwm_driver.*` is the architectural boundary between Core 0 command ingress and Core 1 backend ownership.

Responsibilities:

- accept validated logical channel writes from Core 0
- serialize public writes before mailbox submission
- forward one admitted command across the multicore mailbox
- publish realized state snapshots for Core 0 readers

### 4. Backend Layer

Core 1 owns the backend implementations:

- `pwmdriver/hw_pwm_driver.*`
- `pwmdriver/pio/generator.*`
- `pwmdriver/sw_pwm_driver.*`

These modules own hardware configuration, IRQ or timer paths, and backend-local state.

## Core Ownership

### Core 0

Core 0 owns:

- USB CDC polling
- CLI command parsing and formatting
- I2C transport framing
- I2C deferred command execution
- shared control/status translation
- top-level startup sequencing

### Core 1

Core 1 owns:

- backend initialization
- PWM hardware and PIO state
- software PWM timer callbacks
- PWM-related IRQ handling
- publication of realized channel state

Core 0 must not call backend driver APIs directly.

## Request Flow

### USB CDC Flow

USB commands flow through the following path:

`usb_cdc` -> `cli_shell` -> `device_cli` -> `control_iface` -> `pwmdriver` -> backend

The USB CLI is text-based and supports read, write, LED, reboot, and stop commands.

### I2C Flow

I2C transactions flow through the following path:

`i2c_slave` -> `i2c_control_map` -> `control_iface` -> `pwmdriver` -> backend

Important detail:

- I2C reads can be served directly from the published snapshot or last command status.
- I2C writes are captured in the ISR, deferred into normal Core 0 polling, and then executed through the same shared control path used by USB CDC.

This keeps the ISR transport-focused and avoids running backend-affecting logic in interrupt context.

## Cross-Core Mutation Boundary

`pwm_driver_set_freq()` is the only public cross-core mutation API.

- Core 0 validates and submits logical writes.
- Core 1 applies the write to the selected backend.
- Core 1 publishes the realized state after successful apply.
- Core 0 waits for the result and reports `ok`, `busy`, `invalid`, `timeout`, `unavailable`, or `apply failed` to the caller.

Higher layers should normally enter through `control_iface` rather than calling `pwm_driver_set_freq()` directly.

## Shared State Model

The single source of truth for channel state is the snapshot published by `pwmdriver`.

That means:

- reads report realized backend state, not just the last requested values
- `control_iface` does not keep a second cache
- both USB CDC and I2C observe the same logical channel view

`pulse_count` is monotonic from power-on. `stop` disables output by restoring `freq = 0 Hz` and `duty = 50%`, but it does not reset the counter. For PIO channels, the count is estimated from elapsed time and realized frequency rather than hardware-counted per pulse.

## Channel Layout

| Logical Channels | Backend | GPIOs | Notes |
|------------------|---------|-------|-------|
| `0..7` | Hardware PWM | 1, 3, 5, 7, 9, 11, 13, 15 | PWM slices 0..7, channel B |
| `8..15` | PIO PWM | 0, 2, 4, 6, 8, 10, 12, 14 | Companion pins to the hardware bank |
| `16..23` | Software PWM | 18, 19, 20, 21, 22, 25, 26, 27 | Lower-frequency bank |

## Startup Sequence

The current startup flow is:

1. Core 0 raises the system clock target to 150 MHz when possible.
2. Core 0 initializes the board LED helper.
3. Core 0 initializes TinyUSB CDC and the CLI transport binding.
4. Core 0 launches Core 1 with `pwm_driver_launch()`.
5. Core 0 waits for `pwm_driver_is_ready()`.
6. Core 0 initializes the I2C slave transport.
7. The main loop services `usb_cdc_poll()`, `device_cli_poll()`, and `i2c_slave_poll()`.

When the USB CDC host opens the connection, the CLI prints help once through `device_cli_on_connected()`.

## Where To Read Next

- [Control Protocol](protocol.md) for command syntax and I2C register values
- [Firmware Interfaces](firmware_interfaces.md) for the source-level interfaces
- [PWM Driver Design](detail/pwm_driver_design.md) for backend and mailbox internals
