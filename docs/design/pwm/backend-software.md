# Software PWM Backend

Bank C uses Core 1 timers and GPIOs, sharing CPU time with commands and other
real-time work. Implementation:
[`software_generator.c`](../../../firmware/src/driver/pwm/software/software_generator.c)
and [`software_monitor.c`](../../../firmware/src/driver/pwm/software/software_monitor.c).

## Generation

GPIO transitions follow a 10 microsecond tick. Period and duty are quantized
to that grid; timer latency can add jitter. Use hardware PWM or PIO for tighter
timing or higher frequencies.
[`pwm_driver.c`](../../../firmware/src/driver/pwm/pwm_driver.c) enforces request limits.

Static endpoints use a steady GPIO level without periodic timer work.

## Monitoring

One sampler checks all inputs and timestamps observed transitions, avoiding
per-edge interrupts. Narrow pulses may be missed; out-of-range signals can
alias to a lower frequency or appear static. Averaging cannot recover missed
edges.

Scan spacing depends on callback time and interrupt load. Verify accuracy
with an instrument.
[Operating ranges](../../guides/configuration.md#pwm-operating-ranges).
