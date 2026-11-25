# HIL Test Plan

## Purpose and Scope

Validate PicoPWM on physical Pico (RP2040) and Pico 2 (RP2350) boards: PWM generation, monitoring, CDC/HID/I2C
control, persistent configuration and recovery. This is a manual test plan, not an automated suite or a completed
qualification. Record execution results in [records/](records/README.md).

## CDC Automated Subset

The CDC runner tests the current running roles on the three-bank junction
fixture. Exactly one bank must be a generator and the other two monitors. It
does not change roles, save configuration, or reboot. It drives outputs and
stops all generator channels on completion; it does not restore previous
waveforms. Disconnect other equipment and close dashboards or serial terminals
before running it. See the [HIL runner instructions](../../../tools/hil/README.md)
for installation and invocation.

The runner checks all eight junctions at 100, 500, and 1000 Hz and 25%, 50%,
and 75% duty against both monitors, then checks eight simultaneous outputs
and stop readback. Generator readback is compared with the requested
frequency and duty; each monitor is compared with the generator's realized
readback. Defaults are 5% frequency tolerance (minimum 1 Hz), 5
percentage points duty tolerance, and 3-second monitor and command timeouts.
These are functional-test limits, not backend accuracy specifications. Use
`--frequencies` (100..1000 Hz), `--frequency-tolerance`, `--duty-tolerance`,
`--settle-timeout`, and `--command-timeout` to override them; choose limits
before the run.

The runner writes each run to
`docs/validation/hil/records/latest.md`; see the
[runner instructions](../../../tools/hil/README.md) for overwrite behavior and
[test records](records/README.md) for report requirements. Run separately
after safely configuring each of the other generator-bank passes.

Follow [HIL Test Connections](connections.md) for wiring and safety. Protocol expectations come from the
[CDC shell](../../protocols/usb-cdc.md), [I2C protocol](../../protocols/i2c.md) and
[USB HID reports](../../protocols/usb-hid.md).

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
| Host | Record Windows/macOS/Linux checks for the supported host applications; Direct Dashboard requires desktop Chrome/Edge |
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
| BOOT-02 | Read version and configuration through CDC/HID, and channel count/state through I2C. | Values agree with the artifact and running configuration; host tools connect without a reset loop. |
| CFG-01a | Through HID, change a target bank role and I2C address without saving. Read report 2, then power-cycle. | Running roles/address remain unchanged before reboot; unsaved target changes are lost. |
| CFG-01b | Through CDC, change a bank role and I2C address with one `config set` command; check the new I2C address after reconnect. | CDC saves and reboots automatically; the new values are running and I2C responds at the new address. |
| CFG-02a | Through HID, save known target roles and an address, then power-cycle. | Saved roles/address become running values; I2C responds at the saved address. Restore the fixture configuration afterward. |
| CFG-03a | Through HID, set a target clock profile, inspect report 2 before saving, then save and restart. | Before save, report 2 shows the new target and unchanged running clock; after restart, verify the actual clock or fallback. |
| CFG-03b | Through CDC, set the clock with `config set clock=default`. | CDC saves and reboots automatically; after reconnect, `board get clock` reports the actual running clock. |
| CFG-04 | Through HID, save and restart with an explicitly selected clock profile. | Running clock matches the requested value or reports fallback explicitly; all backend measurements are repeated for that clock. |
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
| IO-03 | At 100 and 400 kHz, exercise I2C register selection, repeated-start reads, short/long reads and queued writes. Include a short read that ends with the master's NACK, then immediately start a fresh read and a repeated-start transaction. | Data, padding and completion status match the protocol; the aborted response does not continue into the next transaction and the bus remains usable. |
| IO-04 | Poll the same active channels over all three transports while changing generator settings. | Each interface eventually reports the same realized state; no deadlock or cross-channel corruption. |
| SAVE-01 | Through HID, issue `SAVE_CONFIG` while generators are active and an I2C controller polls. Capture bus and PWM waveforms. | Save completes or reports failure explicitly; no reboot is expected. Record flash-induced I2C stalls/errors and PWM gaps. |
| SAVE-02 | Through CDC, run `config set` while generators are active and an I2C controller polls. Capture bus and PWM waveforms. | CDC saves and reboots automatically. Record the expected interruption, then reconnect and verify control recovers. |
| REC-01 | Power-cycle after a completed save, then verify every saved field. | Valid configuration survives; outputs start in the documented safe state. |
| REC-02 | Reflash using BOOTSEL on a designated recovery board. | Board enumerates and accepts firmware again. Reflashing alone must not be assumed to erase saved configuration. |

Use finite controller timeouts and bounded retries for the save tests. Record the longest observed stall and required
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
