# Software PWM Detailed Design

This document describes the current software PWM design under `firmware/src/pwmdriver/sw/`.

The scope of this page covers both current software-side modules:

- the software PWM generator backend
- the standalone software PWM monitor prototype

This page is implementation-oriented and reflects the current source tree.

## Source Layout

| File | Responsibility |
|------|----------------|
| `firmware/src/pwmdriver/sw/generator.c` | Software PWM generator backend implementation |
| `firmware/src/pwmdriver/sw/generator.h` | Software PWM generator backend interface |
| `firmware/src/pwmdriver/sw/monitor.c` | Standalone software PWM monitor prototype |
| `firmware/src/pwmdriver/sw/monitor.h` | Standalone software PWM monitor interface |

## Channel and Pin Model

The software PWM bank uses logical channels `16..23` in the unified driver model.

Those channels map to these GPIOs:

| Logical Channel | Backend-local Channel | GPIO |
|-----------------|-----------------------|------|
| 16 | 0 | 18 |
| 17 | 1 | 19 |
| 18 | 2 | 20 |
| 19 | 3 | 21 |
| 20 | 4 | 22 |
| 21 | 5 | 25 |
| 22 | 6 | 26 |
| 23 | 7 | 27 |

The monitor prototype reuses that same physical pin order, so it is intentionally a standalone
alternative that observes the software PWM bank rather than a concurrent companion to the
generator backend.

## Generator Design

### Intent

The software generator backend provides the lowest-cost PWM engine in the current firmware for
the slowest channel range.

The public logical state uses:

- `freq_hz` as `uint32_t`
- `duty` as integer percent `0..100`
- `pulse_count` as a generated-period counter in the unified driver model

### Target Working Range

The recommended working range for the software PWM generator is:

- about `1 Hz .. 1 kHz`

That range follows from the current fixed scheduler tick of `10 us`, which gives a base rate of:

$$
f_{tick} = \frac{1}{10\,us} = 100000\,Hz
$$

The backend then quantizes each PWM channel into an integer number of scheduler ticks per period.

### Generator Timing Model

The software generator uses one repeating timer callback shared across all software channels.

For one requested frequency, the backend computes:

$$
period\_ticks \approx \frac{100000}{freq\_hz}
$$

and then derives the realized exported frequency as:

$$
realized\_freq\_hz \approx \frac{100000}{period\_ticks}
$$

Duty is represented as integer percent and mapped into an integer active window in ticks.

### Static-Level Policy

The software generator treats static outputs the same way as the other generator backends:

- `freq_hz = 0`, `duty = 100` means static high
- `freq_hz = 0`, any other duty means static low

It also treats nonzero-frequency endpoint duties as static outputs:

- `duty = 0` means static low
- `duty = 100` means static high

Those endpoint cases are intentionally removed from the per-tick scheduler path.

### Generator Tradeoffs

The generator path intentionally prefers:

- integer-only timing and publication
- one shared repeating timer callback
- simple static-level handling for `freq_hz = 0` and endpoint duties
- a small active-channel bitmask instead of per-channel timer ownership

over:

- fine-grained sub-tick timing
- high-frequency output targets
- a more complex scheduler structure

## Monitor Design

### Intent

The software monitor is a standalone prototype that observes the software PWM pin bank and reports:

- approximate `freq_hz`
- approximate `duty`

It is intentionally not integrated into `pwm_driver.c` yet.

### Standalone Ownership Model

The software monitor reuses the same GPIO bank as the software PWM generator backend.

That means it should currently be treated as:

- a standalone monitor-oriented implementation for the software PWM bank
- not safe to run alongside the software generator without an explicit integration design

The current code does not include a shared ownership or pin-mux policy between the software
generator and software monitor modules.

### Monitor Working Range

The software monitor should be treated as a best-effort monitor for the slow software PWM bank:

- about `1 Hz .. 1 kHz`

Its meaningful ceiling is not a counter width but the combination of:

- one software GPIO interrupt per edge
- microsecond timestamp granularity
- software latency on Core 1

That makes it a natural match for the software PWM bank and a poor fit for high-rate monitoring.

### Monitor Measurement Model

The monitor uses:

- one GPIO edge interrupt per transition
- microsecond timestamps from `time_us_64()`
- one latest-sample cache per channel

One exported sample is reconstructed from:

1. one high width captured from a rising edge to the next falling edge
2. one full period captured from consecutive rising edges

If no transition is observed for more than one second, the monitor reports a static level as:

- `freq_hz = 0`
- `duty = 0` for static low
- `duty = 100` for static high

### Structural Note

The current software monitor intentionally mirrors the small edge-timestamp monitor pattern used
by the hardware monitor backend.

That duplication is intentional for now because each backend still wants its own pin-bank
constants, public API names, and local limitations stated explicitly. If the software and
hardware monitors continue evolving in parallel, the smallest future cleanup is a private shared
helper for the common edge-capture and idle-timeout logic.