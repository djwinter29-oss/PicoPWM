# I2C Slave Driver Design

The I2C implementation separates bus transport from the PWM register
protocol. The host-visible addresses, payloads, and status values are defined
in the [I2C protocol reference](../protocols/i2c.md).

## Responsibilities

| Layer | Responsibility |
| --- | --- |
| Hardware port | MCU peripheral, FIFO, interrupt, and pin access |
| Transport | Capture transaction bytes and stream response bytes |
| Protocol | Validate frames, select registers, track status, and queue commands |
| Shared control | Apply supported changes and provide realized channel state |

The transport knows where transactions begin and end, but does not interpret
registers or PWM requests. This lets the protocol evolve without coupling it
to peripheral handling.

## Transaction Design

A write is captured as a bounded frame. At a transaction boundary—including
STOP, repeated START, or the start of a read—the protocol layer receives the
frame and decides whether it is valid. Valid changes are queued for the Core 0
foreground loop, then passed through the shared control path. The I2C
interrupt never waits for a PWM backend to apply a change.

At the start of a read, the protocol builds a fresh response from current
state. The transport streams that response; bytes requested beyond its length
are zero-filled. Oversized requests or invalid frames are rejected by the
protocol, which owns their status semantics.

## Timing and Ownership

Work performed in I2C interrupt context must be bounded. It may capture bytes
and update small protocol state, but it must not block, perform unrelated I/O,
or call a PWM backend. If a response needs a consistent multi-byte state
snapshot, synchronization must remain short; the bus may be stretched while
that snapshot is acquired.

Keep protocol changes in the protocol layer and peripheral changes in the
hardware port. Neither layer may bypass the shared control path or take
ownership of PWM runtime state.
