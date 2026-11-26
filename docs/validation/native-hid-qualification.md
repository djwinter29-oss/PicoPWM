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

On each OS, record the application/runtime and HID-library versions and use
the same board, firmware, and default clock. Before testing, declare the
response timeout used for the run; polling deadlines do not cancel a native
USB transfer. If a check cannot be performed safely, mark it **not tested**.

## Procedure

Run each check separately on Windows, Linux, and macOS:

These are manual test criteria; the repository has no native-transfer timing
harness. Use an instrumented client or test harness that issues explicit
feature-report reads and timestamps each native read call. Confirm each sample
causes USB I/O, and record the instrumentation method and version. Do not use
repeated dashboard `/api/status` or `PicoPwmHid.status()` calls as report 1
samples: the host clients cache device identity.

### Normal reads

1. Connect to the selected board and explicitly read HID reports 1 and 2
   100 times each.
2. Record each report's latency, failures, and timeout.

Every read must complete within the predeclared timeout and have the expected
ID, length, and decoded identity or configuration. Confirm each sample causes
USB I/O.

### Idle reconnect

1. Disconnect USB while idle, reconnect, then refresh the device in the
   application.
2. Record disconnect and reconnect behavior.

The application must clear stale state and reread identity and capabilities
before showing the board as connected.

### Disconnect in flight

1. Start a read of report 1 or 2, then disconnect USB before it completes.
2. Reconnect, refresh, and record the operation outcome and recovery time.

The application must report failure or uncertainty rather than success, clear
stale state, and read fresh identity and configuration after reconnect.

### Handle recovery

1. Keep the application process running. Cause a recoverable read error and
   use its normal recovery path to reopen the same board.
2. Record native close/open evidence.

Fresh identity and configuration reads must succeed without restarting the
process. If the error cannot be induced, mark this check **not tested**.

### Transfer stall

1. If a repeatable stall can be induced safely, record the blocked operation,
   recover, then issue a read.
2. Record the stall duration, outcome, and second-read latency.

The second read must complete within the predeclared timeout. Do not retry
mutations while their outcome is uncertain. If a stall cannot be induced,
mark this check **not tested**.

For the 100 normal-read iterations, report median, 95th-percentile, and
maximum latency per report, plus any failures. Declare any timing limit
before the run; there is no project-wide native-transfer latency threshold.

## Status

No native HID qualification is recorded for Windows, Linux, or macOS:

| Platform | Normal reads | Idle reconnect | Disconnect in flight | Handle recovery | Transfer stall |
| --- | --- | --- | --- | --- | --- |
| Windows | Not run | Not run | Not run | Not run | Not run |
| Linux | Not run | Not run | Not run | Not run | Not run |
| macOS | Not run | Not run | Not run | Not run | Not run |

Record physical PWM tests separately under the [HIL plan](hil/test-plan.md).
