# PWM Driver Configuration Design

This document describes how the firmware maps startup configuration to the
fixed channel and backend layout. User steps for selecting bank roles,
changing settings, and resolving setup problems are in the
[Setup and Troubleshooting guide](../../guides/setup-and-troubleshooting.md).

## Configuration Model

PicoPWM builds one firmware image with a stable logical channel interface.
Each of the three banks is assigned a backend family by the board layout and
a role at startup. The role is either generator or monitor; it is not changed
while the firmware is running.

The running configuration is initialized before the real-time channel owner
starts. Host interfaces can update a separate target configuration. The target
is validated and persisted, then becomes the running configuration after
reboot. Invalid or missing persistent data falls back to the electrically
conservative monitor-only roles.

The persisted record contains a magic value, format version, size, clock
profile, bank roles, I2C address, and CRC-32. It is stored in the final 4 KiB
flash sector. The linker excludes that sector from the program region. A
power loss or invalid record falls back to defaults. Flash updates require the
multicore flash-lockout mechanism to be ready; a save is rejected rather than
starting an unsafe erase.

## Fixed Bank Mapping

| Bank | Logical channels | Backend |
| --- | --- | --- |
| A | `0..7` | Hardware PWM |
| B | `8..15` | PIO |
| C | `16..23` | Software |

The default layout exposes 24 logical channels. Each bank maps to one backend
family; startup roles select that backend's generator or monitor
implementation. Backend modules obtain their GPIO assignments from the board
configuration. Channel routing resolves the bank and bank-local channel from
the logical channel number; no separate mutable channel table is maintained.
See [Hardware Setup](../../guides/hardware-setup.md) for the physical mapping
and rationale.

All eight combinations of the three bank roles are valid. GPIO23 and GPIO24
are optional software-only pins for custom boards and are not part of the
standard software bank.

## Backend Resource Constraints

- **Hardware PWM:** eight fixed slice-B pins, one channel per PWM slice. The
  selected GPIO set is fixed; it is not a free choice among all PWM-capable
  pins.
- **PIO:** eight channels across two PIO blocks with four state machines each.
  The state machines and fixed companion slice-A GPIOs are finite resources.
- **Software:** no fixed PWM slice or PIO state machine, but channel count,
  frequency, and measurement capability are limited by CPU, timer, and
  interrupt budgets. Pins must remain valid and uniquely owned.

The pin map is fixed in the board configuration and is not checked at runtime.
Backend range and accuracy constraints are part of the profile contract.
Monitor limits describe measurement capability; generator limits describe
output capability. See the [backend design pages](./) for timing and
measurement details.

## Configuration Invariants

The board mapping and startup configuration must preserve these invariants:

- Logical channel IDs are unique and within the advertised channel count.
- Every GPIO has one owner; input and output ownership are not mixed.
- The selected backend supports the assigned role and local channel index.
- Backend resource assignments do not conflict.
- Transport-visible channel counts agree with the driver layout.
- A bank's generator and monitor implementations do not claim the same
  resources simultaneously.

The USB CDC, USB HID, and I2C interfaces translate requests into the shared
device control path. They do not select a different backend or bypass the PWM
driver ownership boundary. Unsupported operations are rejected through the
shared control result.

## Changing the Layout

The standard Pico and Pico 2 layout is fixed. A custom board requiring a
different physical layout must update the board pin configuration and the
bank-to-backend mapping together. Add or update firmware host tests for the
new mapping and run the firmware test suite. Keep the host-visible channel
model and transport contracts consistent with the new layout.
