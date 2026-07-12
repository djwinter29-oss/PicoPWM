# Hardware PWM Detailed Design

This document describes the current hardware PWM design under `firmware/src/pwmdriver/hw/`.

The scope of this page covers both current hardware-side modules:

- the hardware PWM generator backend
- the standalone hardware PWM monitor prototype

This page is implementation-oriented and reflects the current source tree.

## Source Layout

| File | Responsibility |
|------|----------------|
| `firmware/src/pwmdriver/hw/generator.c` | Hardware PWM generator backend implementation |
| `firmware/src/pwmdriver/hw/generator.h` | Hardware PWM generator backend interface |
| `firmware/src/pwmdriver/hw/monitor.c` | Standalone hardware PWM monitor prototype |
| `firmware/src/pwmdriver/hw/monitor.h` | Standalone hardware PWM monitor interface |

## Channel and Pin Model

The hardware PWM bank uses logical channels `0..7` in the unified driver model.

Those channels map to these GPIOs and slices:

| Logical Channel | Backend-local Channel | GPIO | Slice | PWM Channel |
|-----------------|-----------------------|------|-------|-------------|
| 0 | 0 | 1 | 0 | B |
| 1 | 1 | 3 | 1 | B |
| 2 | 2 | 5 | 2 | B |
| 3 | 3 | 7 | 3 | B |
| 4 | 4 | 9 | 4 | B |
| 5 | 5 | 11 | 5 | B |
| 6 | 6 | 13 | 6 | B |
| 7 | 7 | 15 | 7 | B |

The design intentionally uses the slice-B pins so the physical channel order stays aligned with the measurement-oriented wiring plan.

## Generator Design

### Intent

The hardware generator backend is the highest-accuracy PWM generator in the current firmware.

The public logical state uses:

- `freq_hz` as `uint32_t`
- `duty` as integer percent `0..100`
- `pulse_count` fixed at `0`

The generator treats `freq_hz = 0` as a static-output policy case:

- `freq_hz = 0`, `duty = 100` means static high
- `freq_hz = 0`, any other duty means static low

Nonzero-frequency endpoint duties also resolve directly to static levels:

- `duty = 0` means static low
- `duty = 100` means static high

### Timing Model

The hardware PWM block uses:

- a 16-bit wrap counter, so `TOP + 1` can range from `2` to `65536`
- a fractional clock divider from `1.0` to `255 + 15/16`
- one independent PWM slice per logical channel

The realized PWM frequency is:

$$
f_{pwm} = \frac{f_{sys}}{clkdiv \cdot (TOP + 1)}
$$

In the current implementation the divider is searched in sixteenth-step units:

$$
clkdiv = \frac{div\_x16}{16}
$$

The practical low-end limit comes from the combination of the largest divider and the 16-bit counter:

$$
f_{min} = \left\lceil \frac{f_{sys} \cdot 16}{4095 \cdot 65536} \right\rceil
$$

The theoretical high-end limit comes from the smallest divider and the minimum valid period count of `2`:

$$
f_{max} = \frac{f_{sys}}{2}
$$

### Current Clock Plan and Supported Envelope

The current firmware startup attempts to run `clk_sys` at `150 MHz` and may fall back to `125 MHz` if that clock plan is not accepted by the board.

Some board-level helper paths in this repository also show a `200 MHz` clock plan, so it is useful to show that case as a reference point too.

That gives the following hardware-generator envelope:

| `clk_sys` | Counter-limited minimum | Counter-limited maximum |
|-----------|-------------------------|-------------------------|
| `200 MHz` | about `12 Hz` | `100 MHz` |
| `150 MHz` | about `9 Hz` | `75 MHz` |
| `125 MHz` | about `8 Hz` | `62.5 MHz` |

These are hardware representability limits, not the recommended operating range.

### Target Working Range

The recommended working range for the hardware PWM generator is:

- about `10 Hz .. 1 MHz`

That target range is narrower than the raw hardware envelope for two reasons:

1. The low end should stay above the worst-case counter-and-divider floor even when the startup clock falls back from `150 MHz` to `125 MHz`.
2. The high end should keep enough counter counts per period to preserve useful duty-cycle resolution.

At the top of the recommended range, with `clkdiv = 1`:

- `1 MHz` gives about `200` counts per period at `200 MHz`
- `1 MHz` gives about `150` counts per period at `150 MHz` and about `125` counts per period at `125 MHz`
- that corresponds to duty steps of about `0.5%`, `0.67%`, or `0.8%`, which is still practical

Above that range the backend can still generate outputs, but duty granularity degrades quickly because the 16-bit counter is no longer the limiting factor; the number of available counts per period collapses as frequency rises.

Examples with `clkdiv = 1`:

| Frequency | Counts per period at `150 MHz` | Approximate duty step |
|-----------|--------------------------------|-----------------------|
| `1 MHz` | `150` | `0.67%` |
| `5 MHz` | `30` | `3.3%` |
| `10 MHz` | `15` | `6.7%` |
| `20 MHz` | about `8` | `12.5%` |

For a `200 MHz` reference point with `clkdiv = 1`:

| Frequency | Counts per period at `200 MHz` | Approximate duty step |
|-----------|--------------------------------|-----------------------|
| `1 MHz` | `200` | `0.5%` |
| `5 MHz` | `40` | `2.5%` |
| `10 MHz` | `20` | `5%` |
| `20 MHz` | `10` | `10%` |

For that reason, frequencies above about `1 MHz` are better treated as possible-but-not-targeted outputs rather than as the normal operating range.

### Generator Workflow

The current generator workflow is:

1. accept one logical request in integer `freq_hz` and integer duty percent
2. clamp duty into `0..100`
3. resolve static outputs first for `freq_hz = 0` and endpoint duties
4. otherwise search a local divider window for the best valid `TOP` and divider pair
5. program wrap, divider, and compare level into the slice
6. publish the realized frequency and duty through the backend state

### Generator Tradeoffs

The generator path intentionally prefers:

- integer-only timing search
- realized-state publication in C
- static GPIO drive for `freq_hz = 0`
- no per-period IRQ bookkeeping

over:

- float math in the timing path
- pulse counting through wrap IRQs
- squeezing every representable MHz into the recommended user range

## Monitor Design

### Intent

The hardware monitor is a standalone prototype that observes the same hardware PWM pin bank and reports:

- approximate `freq_hz`
- approximate `duty`

It is intentionally not integrated into `pwm_driver.c` yet.

### Monitor Measurement Model

The monitor uses:

- one GPIO edge interrupt per transition
- microsecond timestamps from `time_us_64()`
- one latest-sample cache per channel

One exported sample is reconstructed from:

1. one high width captured from a rising edge to the next falling edge
2. one full period captured from consecutive rising edges

The monitor then publishes integer frequency and integer duty.

### Monitor Working Range

The hardware monitor does not have a fixed, counter-derived MHz-class range like the generator.

Its meaningful limit is architectural instead:

- one software interrupt per edge
- microsecond timestamp granularity
- best-effort service latency on Core 1

For that reason, the monitor should be treated as:

- suitable for slow PWM signals only
- not suitable for serious kHz-to-MHz measurement work

If the use case needs higher-rate or more repeatable monitoring, use the PIO monitor design instead.

### Monitor Static-Level Policy

If a channel sees no transition for more than one second, the monitor treats the input as a static level and publishes:

- `freq_hz = 0`
- `duty = 0` for static low
- `duty = 100` for static high

### Monitor Pulse Count Policy

The hardware monitor increments a monotonic observed-period counter whenever it reconstructs one
full PWM cycle from a rising edge, the following falling edge, and the next rising edge.

That means `pulse_count` reflects accepted edge-reconstructed cycles, not hardware-counted edges:

- `pulse_count` increments once per completed observed period

## Summary

The current hardware PWM design deliberately separates two roles:

- the generator backend uses the dedicated PWM slices for accurate output generation over a recommended range of about `10 Hz .. 1 MHz`
- the standalone monitor prototype uses GPIO interrupts and microsecond timestamps for slow, best-effort observation only

The 16-bit hardware counter and fractional divider define the raw generator envelope, but the recommended operating range is chosen from both representability and useful duty resolution.