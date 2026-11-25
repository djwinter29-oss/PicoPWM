# Software PWM Backend

The software backend uses Core 1 timer service and GPIO operations instead of
dedicated PWM peripherals. It trades timing precision and range for flexible
resource use. Its work shares Core 1 with command servicing and other
real-time activity.

## Generation

The generator schedules GPIO transitions on a fixed time grid. Period and
duty are quantized to that grid, and timer latency can add jitter. The
recommended range is **1 Hz to 1 kHz**. Use hardware PWM or PIO when tighter
timing or higher frequencies are needed; see [bank design](banks.md) for
accepted request limits.

Static endpoint requests use a steady GPIO level rather than periodic timer
work.

## Monitoring

One shared sampler checks all software-monitor inputs and records observed
transitions using elapsed time. This avoids an interrupt for every input edge
and bounds work per scan, but narrow pulses may be missed. Out-of-range
signals can alias to a lower frequency or appear static; averaging cannot
recover transitions that were never sampled.

The recommended measurement range is **10 Hz to 1 kHz**. Actual scan spacing
depends on callback time and interrupt load; verify measurements against an
instrument when accuracy matters.

Monitoring retains only a latest sample, not waveform history. A static-low
reading and an unavailable sample are not always distinguishable in the
shared channel format. Use an independent instrument when the distinction or
measurement accuracy matters.
