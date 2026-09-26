# Firmware Configuration

PicoPWM is intended to produce multiple firmware builds from one stable logical
channel interface. A build profile selects the role and physical implementation
of each channel; it does not change the host command syntax.

## Build Profiles

The current profile selections are:

- **Generator**: channels produce PWM output signals.
- **Monitor**: channels sample PWM input signals and report measured state.
- **Mixed**: 8 PIO generator channels, 8 software generator channels, and 8
  software monitor channels.

Project-specific custom profiles are the extension point: add another profile
table and CMake selection while preserving the same host control interfaces.

The mixed profile uses logical channels `0..7` for PIO generation, `8..15`
for software generation, and `16..23` for software monitoring. It requires a
custom board map exposing GPIO `23` and `24`; GPIO24 is a board-internal VBUS
sense-related pin on the standard Pico. The standard Pico build rejects this
profile before SDK configuration completes.

GPIO23/24 are optional software-only pins. Normal Pico profiles leave them
unused; custom profiles may assign them only to software generator or software
monitor channels on hardware that exposes them.

A profile is selected at CMake configuration time and compiled into the
firmware. It is not a runtime mode switch: the selected build determines which
backend sources, PIO programs, DMA resources, and channel table are linked.
Build a separate firmware directory for each profile you need:

```sh
cmake -S firmware -B build-generator \
  -DPICO_PWM_PROFILE=generator

cmake -S firmware -B build-monitor \
  -DPICO_PWM_PROFILE=monitor
```

Separate build directories produce separate, reproducible firmware variants.

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

The host-facing commands remain the same for every profile:

- `info`, `version`, `get`, and `status` report the active profile and realized
  channel state.
- `set` applies to output-capable channels and returns `ERR unavailable` for
  monitor-only or disabled channels.
- `stop` applies the profile's safe output reset behavior. For monitor-only
  builds it may be a no-op with a successful response.
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
[Profile Authoring](profile_authoring.md) for how to add a build-time profile
file.
