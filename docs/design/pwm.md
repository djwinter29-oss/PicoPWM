# PWM Design

The PWM driver is the multicore boundary: Core 0 sends requests; Core 1 owns
backend state and timing.

## Request Path and State

Transport and control code must not call backends directly. A channel command
crosses the mailbox to Core 1, which returns the backend result. Commands
unclaimed after one second are cancelled; claimed commands finish rather than
return an uncertain cancellation. The mailbox carries commands, not PWM
edges or monitor samples. The driver validates request limits before backend
application; invalid requests and backend failures are reported to the
requesting transport.

Core 1 publishes the latest configured or measured channel state. Core 0
reads this synchronized snapshot, not a separate requested-state cache.
Generator readings include timing quantization; monitor readings are the
latest sample, not waveform history. The format cannot always distinguish an
unavailable monitor sample from static low.

The driver implementation is
[`pwm_driver.c`](../../firmware/src/driver/pwm/pwm_driver.c); its interface is
[`pwm_driver.h`](../../firmware/src/driver/pwm/pwm_driver.h). Stop disables
generator output without changing saved bank roles.

## Banks and Resources

Each bank has a fixed backend, eight logical channels, and a startup role:

| Bank | Channels | Backend |
| --- | --- | --- |
| A | 0-7 | Hardware PWM |
| B | 8-15 | PIO |
| C | 16-23 | Software PWM |

Each bank generates or monitors for the entire runtime. Saved target settings
become running settings at startup; missing or invalid storage selects
monitor-only roles. Roles cannot switch without rebooting.

Hardware PWM owns fixed slices and pins. PIO owns a limited set of state
machines and pins. Software PWM owns GPIOs and uses CPU/timer service, not
PWM peripherals. Each channel and GPIO has one owner; a backend must support
the bank's role and resources.

### Board Profiles

Hardware PWM uses slice-B GPIOs; PIO uses companion slice-A GPIOs. Neither
accepts arbitrary pins. Software PWM can use other available GPIOs, subject
to unique ownership and board functions such as LED/ADC use. See the
[pin map](../guides/hardware-setup.md#pwm-channels).

On Pico, GPIO24 is VBUS sense and GPIO25 is the LED; neither is in the
default software map. GPIO23/24 require a custom software profile, and
reserved pins may be reclaimed only when board wiring and policy permit.
Pin-layout changes require matching board-profile and backend-resource
updates; channels are not reassigned at runtime.

## Backend Timing

### Hardware PWM (Bank A)

Each channel owns a PWM slice. A divider and period count approximate
frequency; counter steps determine duty resolution, so higher frequencies
leave fewer steps. Readback describes programmed timing, not an electrical
measurement. Zero-frequency requests drive low below 100% duty and high at
100%; endpoint duties also produce static levels at nonzero frequency.
The minimum periodic frequency depends on the running clock; the driver
enforces accepted request limits.

The [generator](../../firmware/src/driver/pwm/hardware/hardware_generator.c)
and [monitor](../../firmware/src/driver/pwm/hardware/hardware_monitor.c)
use the MCU's PWM slices. The monitor uses each slice's single input counter
for separate frequency and high-time windows. A reading takes about half a
second and can mix different moments of a changing signal. Low-frequency
resolution is coarse; high frequencies depend on timely counter servicing.
No absolute maximum or accuracy is qualified.

### PIO (Bank B)

A PIO loop generates PWM without per-edge CPU service. A timing search picks
the period and divider; instruction cycles quantize frequency and duty.
Short periods reduce duty resolution, and readback can differ from the
physical signal. Zero-frequency and endpoint-duty requests use static levels.

Generation and monitoring use the same limited state machines; startup role
selects the owner. The [generator](../../firmware/src/driver/pwm/pio/pio_generator.c)
and [program](../../firmware/src/driver/pwm/pio/pio_generator.pio) produce
the signal. The [monitor](../../firmware/src/driver/pwm/pio/pio_monitor.c)
captures one complete high/low period at a time; readings can lag rapid
changes, and capture time follows the input period. Short pulses, input
synchronization, and capture overhead limit high-frequency measurement.

### Software PWM (Bank C)

GPIO transitions follow a 10-microsecond tick. Period and duty are quantized
to that grid; timer latency can add jitter. Static endpoints use a steady GPIO
level without periodic timer work. Use hardware PWM or PIO for tighter timing
or higher frequencies.

The [generator](../../firmware/src/driver/pwm/software/software_generator.c)
and [monitor](../../firmware/src/driver/pwm/software/software_monitor.c)
share a Core 1 timer. One sampler checks all inputs and timestamps observed
transitions, avoiding per-edge interrupts. Narrow pulses may be missed;
out-of-range signals can alias to a lower frequency or appear static.
Averaging cannot recover missed edges, and scan spacing depends on callback
time and interrupt load.

Request limits and recommended measurement ranges are in
[PWM operating ranges](../guides/configuration.md#pwm-operating-ranges).
Use an instrument when signal accuracy matters.
