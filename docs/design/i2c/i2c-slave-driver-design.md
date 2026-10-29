# I2C Slave Driver Design

This document describes the firmware-side I2C transport boundary. For the
host-visible register map and wire behavior, see the
[I2C Protocol Reference](i2c-protocol.md).

## Ownership

The I2C path is split into two layers:

| Layer | Owns | Does not own |
| --- | --- | --- |
| `driver/i2c/i2c_slave.*` | I2C transaction boundaries, bounded byte capture, and response-byte transmission | Register addresses, expected command lengths, status values, register selection, command queues, or PWM behavior |
| `driver/i2c/i2c_slave_hw.*` | RP2040/RP2350 peripheral, FIFO, interrupt, and GPIO access | Protocol interpretation |
| `app/i2c/i2c_control_map.*` | PWM I2C registers, frame validation, selected register, command status, response formatting, and deferred command execution | Direct peripheral access or PWM backend ownership |
| `device.*` and `pwm_driver.*` | Shared device control and realized PWM state | I2C framing and register semantics |

This boundary lets a future protocol change remain in the application protocol
layer. A different I2C protocol can reuse the byte transport by supplying its
own callbacks.

## Transport Callback Contract

`i2c_slave_init()` receives a static `i2c_slave_ops_t` with two callbacks:

- `write_frame(frame, length, overflow)` is invoked once when a non-empty
  transaction ends at STOP or repeated START, or when a read request begins
  after a write. The frame contains the bytes as received; the driver does not
  treat the first byte as a register or inspect any payload.
- `read_response(response, capacity, length)` is invoked once when a new read
  transaction begins. It supplies the bytes to transmit; the driver does not
  interpret them.

Both callbacks run in the I2C ISR and must have bounded execution time.
Protocol code may validate a frame, update small in-memory protocol state, and
enqueue deferred work there. It must not wait for PWM application, perform
blocking I/O, or call a backend directly. A response callback may briefly wait
for a short cross-core snapshot lock; that can stretch SCL for the duration of
the lock acquisition.

The driver captures at most `I2C_SLAVE_FRAME_CAPACITY` bytes. If a transaction
exceeds that limit, it passes the stored prefix and sets `overflow`; protocol
code is responsible for rejecting it. Responses are limited to
`I2C_SLAVE_RESPONSE_CAPACITY`. A callback failure or a response length beyond
capacity produces an empty response, which the transport zero-pads as the
master clocks additional bytes.

The selected register and what it means belong to the protocol callback state,
not the driver. This keeps register-selection conventions and status-polling
shortcuts out of reusable transport code.

## Transaction Flow

### Write

1. The hardware port reports RX bytes and bus events to the transport ISR.
2. The transport captures bytes without decoding them.
3. At STOP, repeated START, or the start of a read, it submits the captured
   frame and overflow flag to `write_frame`.
4. The protocol validates the frame and updates its own state. If the frame is
   a valid command, it places the command in its own queue.
5. The Core 0 foreground loop calls the protocol's poll function, which removes
   at most one command and forwards it through `device`.

The transport does not queue writes or decide whether a frame is valid. The
protocol queue is owned by the application layer because its payload size,
capacity, and overload status are part of that protocol's behavior.

### Read

1. At the start of a read, the transport asks `read_response` to build one
   response.
2. The callback reads bounded state and returns a byte sequence. It may use
   short, bounded synchronization, which can briefly stretch SCL.
3. The transport feeds response bytes to the peripheral FIFO, then supplies
   zero bytes if the master clocks beyond the response.
4. A later read starts with a fresh callback invocation and can reflect newer
   state.

The callback must build a consistent response snapshot if its protocol
requires multi-byte fields to be atomic. Any lock it takes from ISR context
must have a short, bounded owner; the master may observe clock stretching
while that lock is acquired.

## Hardware Event Handling

`i2c_slave_hw.*` isolates SDK-specific peripheral access from transport logic.
It exposes received bytes, transmit FIFO writes, and STOP, repeated-START,
read-request, transmit-empty, and transmit-abort events. The transport uses
transaction boundaries to deliver complete frames, uses the TX-empty interrupt
only while response bytes remain, and cancels a response after a transmit
abort.

The hardware port may know the MCU peripheral registers and board pins, but it
must not know about the PWM map. Host tests replace the hardware port with a
fake FIFO and event source to exercise the transport independently.

## Change Guidance

- Change register addresses, payload formats, status meanings, frame lengths,
  or deferred command behavior in `app/i2c/i2c_control_map.*` and the
  [I2C Protocol Reference](i2c-protocol.md).
- Change bus event or FIFO handling in `driver/i2c/i2c_slave.*` only when the
  generic transaction transport itself needs different behavior.
- Change MCU peripheral setup or register handling in `driver/i2c/i2c_slave_hw.*`.
- Keep transport tests focused on byte capture, transaction boundaries,
  callback invocation, response streaming, and overflow reporting. Keep
  register and command behavior tests in the protocol-map tests.
