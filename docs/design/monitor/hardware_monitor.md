# Hardware PWM Monitor Design

The hardware monitor measures PWM inputs with the PWM slice counters in input
mode. Source files:

- `firmware/src/driver/pwm/hardware/hardware_monitor.c`
- `firmware/src/driver/pwm/hardware/hardware_monitor.h`

## Method

Bank A is fixed to the 8 slice-B GPIOs, the only pins a slice can use as an
input. A slice has one counter, so each channel alternates two 250 ms windows on
Core 1:

| Window | Slice mode | Result |
| --- | --- | --- |
| Edge | `PWM_DIV_B_RISING`, divider 1 | Rising edges counted: `freq_hz = edges / window`. |
| High | `PWM_DIV_B_HIGH`, divider 4 | Sys-clock ticks while the input is high: `duty = high_time / window`. |

The 16-bit counter wrap flag is polled on every `hw_mon_get()` call (about every
1 ms) and the wraps are accumulated, so the divider is chosen so one poll is
shorter than one wrap. No per-edge CPU work is needed.

## Constraints

- A sample refreshes about every 500 ms, and `get` returns `false` until the first
  pair of windows completes.
- Frequency resolution is the reciprocal of the window (4 Hz), so the monitor is
  reliable above about 20 Hz. Use the software monitor for slow signals.
- If no edge is counted in a window, the monitor publishes `freq_hz = 0` with duty
  `0` or `100` from the sampled GPIO level.
- Duty assumes a stable signal across both windows; a signal that changes between
  windows produces a mixed sample.
- Not tested on hardware.
