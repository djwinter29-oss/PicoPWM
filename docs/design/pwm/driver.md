# PWM Driver Design

The PWM driver presents one channel service across the hardware, PIO, and
software backends. Its central design rule is that Core 1 owns backend state
and timing, while Core 0 owns host requests and responses.

## Boundary and Ownership

| Area | Responsibility |
| --- | --- |
| Host transports | Translate CDC, HID, and I2C requests; format responses |
| Shared control | Provide one transport-independent route to channel state and changes |
| Core boundary | Serialize channel changes and return their outcome |
| Core 1 runtime | Route channels to their configured backend and own backend state |
| Backends | Generate or measure signals using their own timing resources |

Transport and control layers must not call a backend directly. I2C changes
leave interrupt context before entering the shared control path. Bank roles
are selected at startup; runtime changes do not switch backend ownership.
The [configuration design](banks.md) describes the fixed bank model.

## Applying Changes

A channel change crosses from Core 0 to Core 1 through a single-command
mailbox. Only one change is in flight at a time. Core 0 waits for the apply
result, so a response can reflect whether the selected backend accepted the
request.

If Core 1 has not claimed a command by the deadline, Core 0 can cancel it. If
Core 1 has claimed it, the caller waits for the backend result rather than
reporting an uncertain cancellation. Invalid channels and unsupported
requests are rejected before or during backend application; failure is
reported to the requesting transport.

The mailbox carries control changes, not PWM edges or monitor samples. Those
remain owned by Core 1.

## Realized-State Snapshot

Core 1 publishes one latest state per logical channel. Core 0 reads that
snapshot rather than keeping a separate copy of requested state. This keeps
readback aligned with what the backend configured or measured.

Snapshot updates and reads are synchronized so frequency and duty are read as
one coherent value. Generator readback reflects timing quantization; monitor
readback is the latest sample, not waveform history. The shared channel
format cannot always distinguish a static-low input from an unavailable
monitor sample.

## Behavioral Constraints

- The backend owns its timing and resources; upper layers use the logical
  channel service.
- Generator requests and monitor samples use the same realized-state model.
- Stopping disables generator output; it does not change saved bank roles.
- Backend-specific limits and tradeoffs belong in the
  [hardware](backend-hardware.md), [PIO](backend-pio.md), and
  [software](backend-software.md) design pages.
