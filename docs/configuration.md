# Firmware Configuration

PicoPWM produces one firmware build from one stable logical channel interface.
Channel roles are selected once at startup by `pwm_driver_init()`; there is no
build-time profile selection or runtime role switching.

## Startup Bank Configuration

The GPIO map is fixed into three 8-pin banks (see [Pinout](pinout.md)):
Bank A is HW-capable, Bank B is PIO-capable, and Bank C is software-only. All
three banks are configured before Core 1 starts, so every logical channel has a
valid backend and role during runtime.

The three bank roles are selected by `pwm_driver_init()` before Core 1 starts.
They remain fixed for the lifetime of the firmware process, which avoids
switching a GPIO's function while it may be actively driving or reading a
signal. Optional backend-specific settings are supplied through the startup
configuration structure.

The running configuration is immutable until reboot. CDC and I2C may change a
separate target configuration, inspect both target and running settings, and
save the target to flash. A reboot then validates and applies the saved target;
invalid or missing flash data falls back to the electrically conservative
default `A=HW/monitor`, `B=PIO/monitor`, `C=SW/monitor` configuration.

The target record contains a magic value, format version, generation number,
backend/role/address values, and checksum. Two records occupy the final 8 KiB
as alternating slots. The firmware link step rejects images that overlap this
reserved area. A new record is written to the inactive slot, so power loss
during a save preserves the previous valid target.

Each bank is configured independently, so `2^3 = 8` role combinations are
reachable in one firmware image. Backend-family choices are constrained:
Bank A allows HW or SW, Bank B allows PIO or SW, and Bank C allows SW only.

| HW bank (GPIO 1,3,5,7,9,11,13,15) | PIO bank (GPIO 0,2,4,6,8,10,12,14) | SW bank (GPIO 16,17,18,19,20,21,22,28) |
| --- | --- | --- |
| generator | generator | generator |
| generator | generator | monitor |
| generator | monitor | generator |
| generator | monitor | monitor |
| monitor | generator | generator |
| monitor | generator | monitor |
| monitor | monitor | generator |
| monitor | monitor | monitor |

GPIO23/24 are optional software-only pins on custom boards; they are not part
of the SW bank above and are not claimed by hardware PWM or PIO.

## Channel Table

Each logical channel is described by configuration rather than by a hard-coded
range. A channel entry should define at least:

| Field | Meaning |
| --- | --- |
| `id` | Stable host-visible logical channel number. |
| `backend` | Hardware PWM, PIO, software, or monitor backend. |
| `direction` | Input, output, or disabled. |
| `gpio` | Physical GPIO assigned to the channel. |
| `capabilities` | Operations supported by the channel. |
| `min_frequency_hz` | Minimum requested or measurable frequency. |
| `max_frequency_hz` | Maximum requested or measurable frequency. |
| `accuracy_ppm` | Expected generation or measurement accuracy. |

Backend limits are part of the profile contract, not merely implementation
notes. A profile should reject a channel assignment or requested frequency
that exceeds the selected backend's pin, resource, range, or accuracy limits.

## Backend Constraints

| Backend | Channel/resource constraint | Pin constraint | Timing constraint |
| --- | --- | --- | --- |
| Hardware PWM | Fixed at 8 channels, one per RP2040 PWM slice; not expandable by picking a different GPIO. | Fixed to the 8 slice-B GPIOs (`1, 3, 5, 7, 9, 11, 13, 15`); this is one fixed pin set, not a range of compatible choices. | High accuracy, but limited minimum frequency and backend timing envelope. |
| PIO PWM | Fixed at 8 channels (4 state machines per PIO block x 2 blocks); not expandable. | Fixed to the 8 companion slice-A GPIOs (`0, 2, 4, 6, 8, 10, 12, 14`), the other half of the same 8 PWM slice pairs used by hardware PWM. | Broad range, with divider/period quantization and finite PIO resources. |
| Software PWM | No fixed PWM slice or PIO state-machine allocation; channel count is limited by CPU, timer, and interrupt budget. | Any remaining valid, uniquely owned GPIO not claimed by hardware PWM or PIO. | Suitable for simple low-frequency generation/monitoring; polling/timer scheduling limits maximum frequency and accuracy. |

Monitor channels use the corresponding backend constraints as measurement
limits rather than output-generation limits. Use software monitoring for simple
low-frequency measurements; use PIO or hardware monitoring when frequency,
edge timing, or accuracy requirements are high.

The default profiles expose 24 logical channels. The driver uses this table for
logical-channel dispatch; generator and monitor backends retain their
backend-local resources behind the profile mapping.

## Stable CLI Contract

The host-facing commands remain the same regardless of which banks are locked:

- `info`, `version`, `get`, and `status` report device identity and realized
  channel state.
- `bank` locks one bank into a role, or reports all three banks' lock states.
- `set` applies to output-capable channels and returns `ERR unavailable` for
  monitor-only, unlocked, or disabled channels.
- `stop` applies the safe output reset behavior to all locked channels.
- `led` and `reboot` remain board-level commands.

The CLI must not require the host to know which backend owns a logical channel.
Backend and direction details belong in the profile and, where useful, in
machine-readable capability information from `info`.

## Configuration Invariants

A profile must validate these conditions at build time or startup:

- logical channel IDs are unique and within the advertised channel count
- every assigned GPIO has one owner
- input and output ownership are not mixed on the same pin
- the selected backend supports the requested direction
- backend-local channel indices are valid
- the profile's channel count matches the transport register map
- monitor and generator implementations are not enabled simultaneously on a
  shared pin without an explicit ownership policy

See [Architecture](architecture.md) for ownership boundaries and [USB CDC
CLI](control/usb_cdc_cli.md) for the stable interactive interface. See
[PWM Driver Configuration](pwm_driver_config.md) for the fixed Bank A/B/C GPIO map
and startup configuration validation rules.
