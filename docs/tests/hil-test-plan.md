# HIL Test Plan

## Purpose and Scope

Validate PicoPWM on physical Pico (RP2040) and Pico 2 (RP2350) boards: PWM generation, monitoring, CDC/HID/I2C
control, persistent configuration and recovery. This is a manual test plan, not an automated suite or a completed
qualification. Record execution results in [records/](records/README.md).

## CDC Automated Subset

The [CDC runner](../../tools/hil/src/run_hil.py) tests the **current running roles only** on the three-bank junction
fixture. Exactly one bank must be a generator and the other two monitors. It never saves configuration, changes
roles or reboots. It drives outputs and stops all generator channels on completion; it does not restore previous
waveforms. Disconnect other equipment and close dashboards/serial terminals before running it.

On Linux/macOS, the launcher creates `build/hil-venv` and installs the HIL Python project in editable mode when needed:

```sh
tools/host/run-hil.sh --port /dev/ttyACM0 --board pico \
    --firmware build/pico/pico_pwm.uf2 --confirm-fixture
```

Set `PYTHON` to select Python 3.10+ or `VENV_DIR` to override the environment location. The interpreter must support
`venv`; on Debian/Ubuntu install its matching `python3-venv` package if environment creation fails.
The launcher forwards all arguments to the runner. `tools/host/run-hil.sh --help` displays launcher usage without
creating an environment.

Alternatively, install and run directly (including on Windows):

```sh
python -m pip install --editable tools/hil
picopwm-hil --port /dev/ttyACM0 --board pico \
    --firmware build/pico/pico_pwm.uf2 --confirm-fixture
```

Use the appropriate port (for example `COM5` on Windows), board and actual flashed artifact. Artifact identity and
board model are operator supplied: CDC cannot verify the flashed binary's hash or identify the MCU here.
Use an editable installation: this repository-local tool locates the fixed record directory relative to its source.

The runner checks all eight junctions at 100, 500 and 1000 Hz and 25%, 50%, 75% duty, comparing both monitors
against generator readback. It then checks eight simultaneous outputs and stop readback. Defaults are 5% frequency
tolerance (at least 1 Hz), 5 percentage points duty tolerance, a 3-second monitor settling timeout and a 3-second CDC
command timeout. These are functional-test limits, **not backend accuracy specifications**. Override with
`--frequencies` (100..1000 Hz), `--frequency-tolerance`, `--duty-tolerance`, `--settle-timeout` and
`--command-timeout`; choose limits before the run.

Every started run atomically replaces `docs/tests/records/latest.md` with metadata, results, unexecuted scope and a
CDC transcript, including failures opening the port or reading the artifact. Invalid CLI arguments do not replace
the record. Exit status is zero for a passing subset and nonzero for failure; report-write failures are also errors.
If cleanup cannot confirm `stop`, disconnect board power. Copy the record to a dated filename before the next run
to retain history. Run separately after safely configuring each of the other generator-bank passes.

Hardware-free runner regression tests:

```sh
python -m unittest discover -s tools/hil/tests -p 'test_*.py'
```

Follow [HIL Test Connections](hil-test-connection.md) for wiring and safety. Protocol expectations come from the
[CDC shell](../guides/usb_cdc_shell.md), [I2C protocol](../design/i2c/i2c-protocol.md) and
[HID report reference](../design/usb/hid-report-reference.md).

## Preconditions

1. Select the exact firmware artifact; record its version, commit and SHA-256 before flashing.
2. Record the board, host OS/application, instruments, wiring and running configuration.
3. Run relevant host tests and both board builds. Attach their results, but do not count them as hardware results.
4. Start with the SDK default clock, known bank roles and no external load beyond the test fixture.
5. Define measurement tolerances and timeouts before testing. Use the backend design limits and instrument
   resolution; do not choose limits after seeing results.

Do not erase a board's existing configuration without first recording it and obtaining the owner's approval.
Disconnect loopbacks before changing roles that could make both ends outputs.

## Coverage Matrix

| Dimension | Required baseline coverage |
| --- | --- |
| Board | Pico and Pico 2; qualify each artifact separately |
| Backend | Hardware, PIO and software, each as generator and monitor |
| Channels | Every channel in each bank; single-channel and eight-channel operation |
| Transport | CDC, HID and I2C; compare realized channel state across interfaces |
| Host | Record Windows/macOS/Linux checks for the supported host applications; WebHID requires desktop Chrome/Edge |
| Clock | SDK default baseline; saved 150/200/250 MHz profiles are separate, explicitly identified runs |
| I2C speed | 100 kHz baseline and 400 kHz |

Use the three shared-junction passes in the connection guide for routine functional checks, with exactly one
generator bank and two monitor banks. Use pairwise wiring for backend range and accuracy checks. If a generator and monitor do not share a test frequency,
use an independent signal source or measurement instrument instead of exceeding either backend's limits.

## Test Cases

### Startup and Configuration

| ID | Procedure | Expected result |
| --- | --- | --- |
| BOOT-01 | Boot with invalid/erased configuration on a designated test board. Inspect configuration before attaching loopbacks. | Monitor-only defaults, I2C address `0x40`, SDK default clock, and no active PWM generator outputs. |
| BOOT-02 | Read version, configuration and channel state through CDC, HID and I2C where exposed. | Values agree with the artifact and running configuration; host tools connect without a reset loop. |
| CFG-01 | Change a target bank role and I2C address without saving. Read running/target values, then power-cycle. | Running roles/address remain unchanged until reboot; unsaved target changes are lost. |
| CFG-02 | Save known roles and an address, then power-cycle. | Saved roles/address become running values; I2C responds at the saved address. Restore the fixture configuration afterward. |
| CFG-03 | Change the clock profile through CDC and HID, observing HID report 2 before saving. | Both interfaces share the target profile; running clock does not change immediately. |
| CFG-04 | Save an explicitly selected clock profile and reboot. | Running clock matches the requested value or reports fallback explicitly; all backend measurements are repeated for that clock. |
| CFG-05 | Exercise invalid bank, address, profile and missing HID confirmation/overclock flags. | Requests are rejected without changing the prior valid target or saving it. |

Only run non-default clock tests with operator approval and BOOTSEL recovery available. Do not treat successful
booting as evidence of timing accuracy or long-term overclock stability.

### PWM Generation and Monitoring

For each connection pass, begin with one channel, then test all eight. Record requested state, reported realized
state and independent instrument measurements separately.

| ID | Procedure | Expected result |
| --- | --- | --- |
| PWM-01 | Apply supported low, middle and upper-range frequencies at 25%, 50% and 75% duty. | Instrument results meet the predeclared tolerances around the realized state; receiving monitors settle within the declared timeout. |
| PWM-02 | Sweep frequencies and duty cycles while other channels remain active. | No unintended changes on other channels; quantization is reflected in realized state. |
| PWM-03 | Apply 0% and 100% duty and the documented zero-frequency static cases. | GPIO levels and realized state match backend semantics; static outputs are not reported as oscillating signals. |
| PWM-04 | Start active generators, send `stop`, then restart them. | All generator outputs go low and report 0 Hz/0%; monitors remain configured as inputs; restart works. |
| PWM-05 | Exercise backend frequency boundaries, invalid channels and writes to monitor channels through each transport. | Behavior matches documented validation; rejected requests leave the previous state intact. |
| MON-01 | Apply independently measured inputs across each monitor's supported range, including duty changes. | Frequency/duty readings meet declared tolerances after the measurement window. |
| MON-02 | Change a driven input to static low, then static high. | No stale oscillating measurement remains beyond the declared no-signal timeout. Record backend-specific static reporting. |
| LOAD-01 | Run eight active software generator channels while polling CDC, HID and I2C for at least 60 seconds. | No reset, deadlock or lost channel control; instrument jitter stays within the declared limit. Record polling rate and observed worst-case jitter. |

Loopback alone cannot establish absolute accuracy. For software timing ownership, use a debugger or an explicitly
identified diagnostic build to observe the tick's executing core; expect Core 1. Do not claim this is established
solely by a waveform or a passing initialization check.

### Transport, Save and Recovery

| ID | Procedure | Expected result |
| --- | --- | --- |
| IO-01 | Exercise CDC editing/help and valid/invalid channel commands; disconnect/reconnect USB. | Shell remains usable; failures are explicit; reconnection does not corrupt running configuration. |
| IO-02 | Exercise HID identity, configuration, bank status, actions and completion status. | Report lengths, transaction IDs and results match the reference; reconnecting the host remains possible. |
| IO-03 | At 100 and 400 kHz, exercise I2C register selection, repeated-start reads, short/long reads and queued writes. | Data, padding and completion status match the protocol; no permanent bus lockup. |
| IO-04 | Poll the same active channels over all three transports while changing generator settings. | Each interface eventually reports the same realized state; no deadlock or cross-channel corruption. |
| SAVE-01 | Save configuration while generators are active and an I2C controller polls. Capture bus and PWM waveforms. | Save completes or reports failure explicitly; control recovers without reset. Record flash-induced I2C stalls/errors and PWM gaps, rather than assuming uninterrupted operation. |
| REC-01 | Power-cycle after a completed save, then verify every saved field. | Valid configuration survives; outputs start in the documented safe state. |
| REC-02 | Reflash using BOOTSEL on a designated recovery board. | Board enumerates and accepts firmware again. Reflashing alone must not be assumed to erase saved configuration. |

Use finite controller timeouts and bounded retries for SAVE-01. Record the longest observed stall and the required
retry behavior. Power interruption during flash writes is a separate destructive test: perform it only with explicit
approval, a recoverable board and a recorded recovery procedure.

## Acceptance and Reporting

Assign each case **PASS**, **FAIL**, **SKIP** or **BLOCKED** with evidence. A skipped case is not a pass.
Record numerical frequency error, duty error, jitter, monitor settling time and transport/save latency where relevant;
include the chosen limits, instrument uncertainty, workload and sample duration.

For a complete baseline, execute all applicable cases on both boards at the SDK default clock. Report additional
clock profiles and host platforms separately, and list uncovered combinations. Timing or recovery failures block
qualification of the affected artifact/configuration until resolved and retested.

Store dated reports and captures in [records/](records/README.md), referring to test IDs and connection passes.
No release or hardware qualification is implied until those results exist.
