# PIO PWM Backend

Bank B uses one PIO state machine per channel and a
[fixed GPIO map](../../guides/hardware-setup.md#pwm-channels).

## Generation

A PIO loop generates PWM without per-edge CPU service. A timing search picks
the period and divider; instruction cycles quantize frequency and duty.
Implementation:
[`pio_generator.c`](../../../firmware/src/driver/pwm/pio/pio_generator.c)
and [`pio_generator.pio`](../../../firmware/src/driver/pwm/pio/pio_generator.pio).

Short periods reduce duty resolution. At high frequencies, duty readback
can differ from the physical signal. Zero-frequency and endpoint-duty
requests use static levels.

## Monitoring

[`pio_monitor.c`](../../../firmware/src/driver/pwm/pio/pio_monitor.c) captures
one complete high/low period at a time. Readings can lag rapid changes;
capture time follows the input period. Short pulses, input synchronization,
and capture overhead limit high-frequency measurement. Verify with an instrument.

Generator and monitor share the same PIO resources; the startup role chooses
the owner. [Operating ranges](../../guides/configuration.md#pwm-operating-ranges).
