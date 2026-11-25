# Native HID Qualification

Qualify native USB transfers separately on Windows, Linux, and macOS; unit
tests and polling deadlines are not transfer deadlines. Record OS, app/runtime
and HID-library versions, board/firmware, connection, timing, outcome, and cleanup.

## Safety and Preconditions

Before any output-driving test, obtain operator confirmation of the board,
flashed artifact, port, fixture, and permission to drive outputs. Follow the
[HIL connection guide](hil/connections.md); do not change roles, save flash,
reboot, or rewire automatically to prepare a test. If output state is
uncertain, disconnect board power; the host is not an emergency stop.

Mark a stall that cannot be reproduced safely as **not tested**, not passed.

## Checks

- Check normal command latency.
- Disconnect and reconnect while idle and during a command.
- Verify selected-board identity and capability refresh after reconnect.
- Verify handle cleanup during normal and exceptional shutdown.
- If a repeatable native stall can be induced safely, measure its effect on
  the operation and a second call to the same board.
- Record partial configuration changes and uncertain outcomes; a timeout does
  not cancel a native transfer.

## Status

No native HID qualification is recorded for Windows, Linux, or macOS:

| Platform | Disconnect/reconnect | Transfer stall | Handle cleanup |
| --- | --- | --- | --- |
| Windows | Not run | Not run | Not run |
| Linux | Not run | Not run | Not run |
| macOS | Not run | Not run | Not run |

Record physical PWM tests separately under the [HIL plan](hil/test-plan.md).
