# PIO PWM Generator Design

The PIO generator produces continuous PWM output using one PIO state machine
per configured channel. Source files:

- `firmware/src/pwmdriver/generator/pio_generator.c`
- `firmware/src/pwmdriver/generator/pio_generator.h`
- `firmware/src/pwmdriver/generator/pio_generator.pio`

## Resource Constraints

The PIO generator is capped at 8 channels total: two PIO blocks, four state
machines each. The default profile fixes these 8 channels to the 8 companion
slice-A GPIOs paired with the hardware PWM slices (`0, 2, 4, 6, 8, 10, 12,
14`); this is one fixed pin set, not a free choice among routable GPIOs. A
custom profile must still ensure that each channel has a valid PIO state
machine and routable GPIO. Generator and monitor channels must not claim the
same PIO resource.

The PIO timing divider and period counter quantize the realized frequency, so
the applied frequency may differ from the requested frequency. The profile's
current target range is about `1 Hz .. 1 MHz`.

## Operation

The compact PIO program loads a period count and duty threshold, loops
continuously, and drives the side-set output. C code selects timing values,
handles static output cases, and publishes realized frequency, duty, and pulse
state through `pwmdriver`.

The public policy is:

- `freq_hz = 0`, `duty = 100`: static high
- `freq_hz = 0`, other duty: static low
- nonzero frequency with `duty = 0` or `100`: static output level

```mermaid
flowchart TD
    Request[Logical frequency and duty] --> Static{Zero frequency or endpoint duty?}
    Static -- Yes --> Level[Drive static GPIO level]
    Static -- No --> Search[Search PIO divider and period]
    Search --> Program[Program PIO state machine]
    Level --> Publish[Publish realized state]
    Program --> Publish
```

See [PWM Driver Design](../pwm_driver_design.md) for profile routing and
mailbox ownership.
