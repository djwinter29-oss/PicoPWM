# Hardware PWM Backend

Bank A uses the MCU's PWM slices with a
[fixed GPIO map](../../guides/hardware-setup.md#pwm-channels).

## Generation

Each channel owns a slice. A divider and period count approximate frequency;
counter steps determine duty resolution. Higher frequencies leave fewer steps.
Readback describes programmed timing, not an electrical measurement.
See
[`hardware_generator.c`](../../../firmware/src/driver/pwm/hardware/hardware_generator.c).

A zero-frequency request drives low below 100% duty and high at 100%.
Endpoint duties also produce static levels at nonzero frequency.
The minimum periodic frequency depends on the running clock; request limits
are enforced in
[`pwm_driver.c`](../../../firmware/src/driver/pwm/pwm_driver.c).

Use an instrument to verify frequency, duty, and signal integrity.
[Operating ranges](../../guides/configuration.md#pwm-operating-ranges).

## Monitoring

[`hardware_monitor.c`](../../../firmware/src/driver/pwm/hardware/hardware_monitor.c)
uses each slice's single input counter for separate frequency and high-time
windows. A reading takes about half a second and can mix different moments
of a changing signal.

Low-frequency resolution is coarse; high frequencies depend on timely
counter servicing. No absolute maximum or accuracy is qualified.
Use another backend for slower signals and an instrument for accuracy checks.
