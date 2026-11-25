# PWM Bank Design

Three banks have fixed backends and GPIOs.
[Pin map](../../guides/hardware-setup.md#pwm-channels) ·
[Role configuration](../../guides/configuration.md#bank-roles).

## Fixed Banks, Startup Roles

Each bank generates or monitors for the entire runtime. Saved target settings
become running settings at startup; missing or invalid storage selects
monitor-only roles. Roles cannot switch without rebooting.

## Request Validation

[`pwm_driver.c`](../../../firmware/src/driver/pwm/pwm_driver.c) checks
[request limits](../../guides/configuration.md#pwm-operating-ranges) before
backend application.

A `0 Hz` request selects a static output level rather than periodic PWM.

## Resource Ownership

- Hardware PWM owns fixed slices and pins.
- PIO owns a limited set of state machines and pins.
- Software PWM owns GPIOs and uses CPU/timer service, not PWM peripherals.

## Board Profiles

Hardware PWM uses slice-B GPIOs; PIO uses the companion slice-A GPIOs.
Neither accepts arbitrary pins. Software PWM can use other available GPIOs,
subject to unique ownership and board functions such as LED/ADC use.

On Pico, GPIO24 is VBUS sense and GPIO25 is the LED; neither is in the default
software map. GPIO23/24 require a custom software profile, and reserved pins
may be reclaimed only when board wiring and policy permit.

## Design Invariants

- Each logical channel and GPIO has one owner.
- A selected backend must support the bank's role and resources.
- Generator and monitor implementations must not claim the same resources.
- Backend-specific timing behavior is described in the
  [hardware](backend-hardware.md), [PIO](backend-pio.md), and
  [software](backend-software.md) design pages.

Pin-layout changes require matching board-profile and backend-resource
updates. Channels are not reassigned at runtime.
