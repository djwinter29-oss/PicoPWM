# HIL Test Plan

Manual qualification for Pico (RP2040) and Pico 2 (RP2350): PWM generation,
monitoring, CDC/HID/I2C, saved configuration, and recovery.

- [CDC automated subset](#cdc-automated-subset)
- [Preconditions](#preconditions)
- [Coverage matrix](#coverage-matrix)
- [Startup and configuration](#startup-and-configuration)
- [PWM generation and monitoring](#pwm-generation-and-monitoring)
- [Transport, save and recovery](#transport-save-and-recovery)
- [Acceptance and reporting](#acceptance-and-reporting)

## CDC Automated Subset

The [CDC runner](../../../tools/hil/README.md) requires the
[junction fixture](connections.md), one running generator bank, and two
monitor banks. It never changes roles, saves, or reboots. It drives outputs,
then stops all generators without restoring previous waveforms.
Disconnect other equipment and close dashboards/terminals first.

It tests eight junctions at 100/500/1000 Hz and 25/50/75% duty, simultaneous
outputs, and stop readback. Generators are compared with requested values;
monitors with realized generator values. Default limits are 5% frequency
(minimum 1 Hz), 5 duty percentage points, and 3-second command/monitor timeouts.
These are functional limits, not accuracy specifications.
Override with `--frequencies` (100..1000 Hz), `--frequency-tolerance`,
`--duty-tolerance`, `--settle-timeout`, and `--command-timeout` before the run.

Runs replace `docs/validation/hil/records/latest.md`; preserve needed reports
under dated names first. Repeat after safely configuring each generator-bank
pass. Wire expectations come from [CDC](../../protocols/usb-cdc.md),
[HID](../../protocols/usb-hid.md), and [I2C](../../protocols/i2c.md).

## Preconditions

1. Record firmware version, commit, and artifact SHA-256 before flashing.
2. Record board, host OS/app, instruments, wiring, and running configuration.
3. Run relevant host tests and both board builds. Attach results separately
   from hardware evidence.
4. Use the SDK default clock, known roles, and no load beyond the fixture.
5. Choose tolerances/timeouts using [operating ranges](../../guides/configuration.md#pwm-operating-ranges)
   and instrument resolution before testing, never after seeing results.

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

Routine checks use three junction passes, each with one generator and two
monitors. Use pairwise wiring for range/accuracy tests. Where ranges do not
overlap, use an independent source or instrument rather than exceed limits.

## Test Cases

### Startup and Configuration

| ID | Check | Pass condition |
| --- | --- | --- |
| BOOT-01 | Boot invalid/erased configuration on a designated test board; inspect before wiring. | Monitor-only defaults, address `0x40`, SDK clock, no active generators. |
| BOOT-02 | Read CDC/HID version/configuration and I2C channel count/state. | Values match the artifact/configuration; hosts connect without reset loops. |
| CFG-01a | [Discard unsaved HID changes](#cfg-01a-discard-unsaved-hid-changes). | Targets change without changing running values; power-cycle discards them. |
| CFG-01b | [Apply CDC changes](#cfg-01b-apply-cdc-changes). | Save/reboot applies both values; I2C responds at the new address. |
| CFG-02a | [Persist HID changes](#cfg-02a-persist-hid-changes). | Saved values survive power-cycle; fixture configuration is restored. |
| CFG-03a | [Apply an HID clock profile](#cfg-03a-and-cfg-04-apply-an-hid-clock-profile). | Target changes before save; actual clock or fallback is recorded after reboot. |
| CFG-03b | Run `config set clock=default`, then reconnect and `board get clock`. | CDC saves/reboots; readback gives the actual running clock. |
| CFG-04 | [Repeat with explicit clock profiles](#cfg-03a-and-cfg-04-apply-an-hid-clock-profile). | Requested clock or explicit fallback is reported; repeat all backend measurements. |
| CFG-05 | Try invalid banks, addresses, profiles, and missing HID confirmation/overclock flags. | Reject without altering valid targets or saving. |

Use known test values and record target/running configuration separately.
`config get` reports running values. Non-default clocks require operator
approval and BOOTSEL recovery; successful booting does not qualify timing
accuracy or long-term stability.

#### CFG-01a: Discard Unsaved HID Changes

1. Through HID, change a target bank role and I2C address without saving.
2. Read report 2 and record the changed targets and unchanged running values.
3. Power-cycle, then verify the unsaved targets were discarded and running
   configuration is unchanged.

#### CFG-01b: Apply CDC Changes

1. Through CDC, use one `config set` command to change a bank role and I2C
   address to known test values.
2. After reboot, reconnect, verify `config get`, and check the new I2C address.

#### CFG-02a: Persist HID Changes

1. Through HID, stage known bank roles and an I2C address, then issue
   `SAVE_CONFIG` with the required confirmation.
2. Verify save success, power-cycle, and check the running values and I2C
   response.
3. Stage and save the fixture configuration through HID, power-cycle, then
   verify the restored running values before reconnecting the fixture.

#### CFG-03a and CFG-04: Apply an HID Clock Profile

1. Through HID, select the test clock profile and read report 2. Confirm the
   target changed while the running clock remains unchanged.
2. Use `SAVE_REBOOT` with the required overclock acknowledgement.
3. After reconnecting, verify the actual clock or fallback and repeat backend
   measurements for that profile.

### PWM Generation and Monitoring

For each pass, test one channel, then all eight. Record requested, realized,
and independently measured values separately.

| ID | Check | Pass condition |
| --- | --- | --- |
| PWM-01 | Low/middle/upper-range frequencies at 25/50/75% duty. | Instrument agrees with realized state within chosen tolerances; monitors settle before timeout. |
| PWM-02 | Sweep frequency/duty with other channels active. | Other channels stay unchanged; readback reflects quantization. |
| PWM-03 | Zero frequency and 0/100% duty cases. | Static GPIO levels/readback match backend semantics, not oscillating signals. |
| PWM-04 | Start, stop all generators, and restart. | Outputs go low at 0 Hz/0%; monitors stay inputs; restart works. |
| PWM-05 | Boundaries, invalid channels, and monitor writes on each transport. | Validation matches the reference; rejection preserves previous state. |
| MON-01 | Independently measured inputs across each monitor's range and duties. | Settled readings meet frequency/duty tolerances. |
| MON-02 | Drive static low, then high. | Stale oscillation clears before the no-signal timeout; record static readback. |
| LOAD-01 | Eight software generators plus CDC/HID/I2C polling for at least 60 seconds. | No reset, deadlock, or lost control; jitter meets limits. Record polling rate and worst jitter. |

Loopback does not establish absolute accuracy. Verify software tick ownership
on Core 1 with a debugger or identified diagnostic build, not a waveform or
initialization check.

### Transport, Save and Recovery

| ID | Check | Pass condition |
| --- | --- | --- |
| IO-01 | CDC editing/help, valid/invalid channel commands, USB reconnect. | Explicit failures; usable shell and unchanged running configuration. |
| IO-02 | HID identity, configuration, bank state, actions, and completion. | Lengths, transaction IDs, and results match the reference; host reconnects. |
| IO-03 | [I2C short-read recovery](#io-03-i2c-short-read-recovery). | Data, padding, and status match the reference; next transaction works. |
| IO-04 | Poll all transports while changing outputs. | Eventual agreement on realized state; no deadlock or cross-channel corruption. |
| SAVE-01 | [HID save under load](#save-01-hid-save-without-reboot). | Explicit success/failure, no reboot; record I2C stalls/errors and PWM gaps. |
| SAVE-02 | [CDC save under load](#save-02-cdc-save-and-reboot). | Automatic save/reboot; control recovers after reconnect. |
| REC-01 | Power-cycle after save; verify every field. | Configuration survives; outputs start safely. |
| REC-02 | BOOTSEL reflash on a designated recovery board. | USB enumerates and accepts firmware; do not assume saved configuration was erased. |

#### IO-03: I2C Short-Read Recovery

Run at both 100 kHz and 400 kHz:

1. Select and read a register using a repeated START.
2. Read a response using a short read, ending it with the master's normal NACK.
3. Immediately start a fresh read, then repeat with a STOP followed by a separate read.
4. Exercise long reads and queued command writes.

Data, padding, and command status must match the
[I2C protocol](../../protocols/i2c.md). The short response must not leak into
the next transaction, and the bus must remain usable.

For SAVE-01/02, run a generator and an I2C polling controller together.
Capture bus/PWM waveforms and record generator, polling rate, stalls/errors,
and output gaps.

#### SAVE-01: HID Save Without Reboot

1. Issue confirmed HID `SAVE_CONFIG` while generation and polling remain active.
2. Record the result and flash-induced bus/PWM interruptions. No reboot is expected.

#### SAVE-02: CDC Save and Reboot

1. Run CDC `config set` for the test configuration under the same load.
2. Record reboot interruption. Reconnect and verify the new
   running configuration and recovered control.

Use finite controller timeouts and bounded retries; record longest stalls
and retry behavior. Power interruption during flash writes is a separate
destructive test requiring explicit approval, a recoverable board, and a
recorded recovery procedure.

## Acceptance and Reporting

Mark each case **PASS**, **FAIL**, **SKIP**, or **BLOCKED**, with evidence.
Skipped checks are not passes. Record frequency/duty error, jitter, settling
time, and transport/save latency, with limits, instrument uncertainty,
workload, and sample duration.

A complete baseline needs all applicable cases on both boards at SDK defaults.
Report extra clock profiles/platforms separately and list uncovered combinations.
Timing or recovery failures block affected artifacts/configurations until fixed and retested.

Store dated [reports and captures](records/README.md) with case IDs and passes.
No release or hardware qualification is claimed without results.
