# PWM Bank Design

PicoPWM presents a stable logical channel model over three fixed backend
banks. The [hardware setup guide](../../guides/hardware-setup.md) is the
reference for the physical channel map; the
[configuration guide](../../guides/configuration.md) explains how users
select bank roles.

## Fixed Banks, Startup Roles

Each bank has one backend family. Startup configuration selects whether that
bank generates or monitors PWM. The role remains fixed until reboot so that
backend resources are initialized and owned consistently for the full runtime.

The running configuration is distinct from a requested target configuration.
Changes are validated and saved before they become active at startup. If
persistent configuration is missing or invalid, firmware uses conservative
monitor-only roles.

## Accepted Generator Requests

These are firmware request limits, not recommended operating ranges or
accuracy guarantees. Backend-specific timing and measurement recommendations
are described in the [backend design pages](./).

| Bank | Backend | Periodic frequency requests |
| --- | --- | --- |
| A | Hardware PWM | 10 Hz-10 MHz |
| B | PIO | 1 Hz-10 MHz |
| C | Software PWM | 1 Hz-10 kHz |

A `0 Hz` request selects a static output level rather than periodic PWM.

## Resource Ownership

The bank layout reflects the resources of the target MCU:

- Hardware PWM has a fixed set of slice and pin assignments.
- PIO has a finite number of state machines and routable pins.
- Software PWM uses CPU time, timer service, and GPIO ownership rather than
  dedicated PWM peripherals.

The fixed layout avoids runtime backend reassignment. A channel's logical
identity remains stable while its configured bank role determines whether it
is used for generation or monitoring.

## Board Profiles

Hardware PWM and PIO use fixed pin assignments: hardware PWM uses one set of
slice-B GPIOs, while PIO uses the companion slice-A GPIOs. Neither can be
assigned to an arbitrary pin. Software PWM can use another available GPIO,
subject to unique pin ownership and board functions such as LED and ADC use.

On the standard Pico, GPIO24 is VBUS sense and GPIO25 is the onboard LED; these
pins are not in the default software map. GPIO23 and GPIO24 may be used only by
a custom software-PWM profile. A custom profile may reclaim a reserved pin
only when the board wiring and policy support it.

## Design Invariants

- Each logical channel and GPIO has one owner.
- A selected backend must support the bank's role and resources.
- Generator and monitor implementations must not claim the same resources.
- Host interfaces share the same channel model and control path.
- Backend-specific timing and measurement limits remain with each
  [backend design](./).

Changing the physical layout means changing the board profile and backend
resource assignments together. Do not add dynamic channel reassignment unless
the product requires it; it would complicate resource ownership and the
host-visible channel contract.
