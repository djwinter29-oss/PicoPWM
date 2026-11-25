# CDC Shell Design

The CDC shell is the serial command interface to PicoPWM. This page describes
its design boundaries; the [USB CDC shell reference](../protocols/usb-cdc.md)
is the reference for command syntax and behavior.

## Responsibilities

| Area | Owns |
| --- | --- |
| USB transport | Moving bytes, connection state, and bounded input/output |
| Shell | Line editing, command selection, help, and completion |
| Command groups | Argument validation, calling shared device services, and formatting results |
| Shared services | Device state and supported changes, independent of transport |

The shell runs on Core 0. A command that changes channel state uses the shared
control path; it does not call a PWM backend. See
[Firmware Architecture](architecture.md) and the
[PWM driver design](pwm/driver.md) for those ownership rules.

## Command Handling

The shell completes one command at a time in the normal application loop.
Transport callbacks only move data; they do not parse or execute commands.
Before a command changes state, its full input is validated and the owning
service is asked to perform the operation. The response reflects that
service's result rather than assuming success.

New commands should fit an existing command group where possible. Add a new
group only for a distinct area of device control. Keep the command contract in
the [shell reference](../protocols/usb-cdc.md), not duplicated here.

## Sessions and Bounded I/O

Each connection has a bounded shell session. Disconnecting clears pending
input, queued output, and any incomplete command line before the next session.
A disconnect does not cancel device work that has already started, and a
response may be lost if the connection ends before it is sent.

Input is not silently discarded when its queue is full, and output is accepted
only when the complete response fits. Multi-line responses can therefore be
incomplete if the output queue fills. Keep responses concise and report a
failed write rather than presenting it as a successful response.
