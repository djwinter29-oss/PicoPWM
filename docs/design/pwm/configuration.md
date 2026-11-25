# PWM Configuration Design

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

## Resource Ownership

The bank layout reflects the resources of the target MCU:

- Hardware PWM has a fixed set of slice and pin assignments.
- PIO has a finite number of state machines and routable pins.
- Software PWM uses CPU time, timer service, and GPIO ownership rather than
  dedicated PWM peripherals.

The fixed layout avoids runtime backend reassignment. A channel's logical
identity remains stable while its configured bank role determines whether it
is used for generation or monitoring.

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
