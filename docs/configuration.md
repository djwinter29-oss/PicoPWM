# Firmware Configuration

PicoPWM is intended to produce multiple firmware builds from one stable logical
channel interface. A build profile selects the role and physical implementation
of each channel; it does not change the host command syntax.

## Build Profiles

The planned profiles are:

- **Generator**: channels produce PWM output signals.
- **Monitor**: channels sample PWM input signals and report measured state.
- **Custom**: a project-specific channel table may mix supported backends and
  channel capabilities.

A profile should be selected at CMake configuration time and recorded in the
firmware identity or `info` response. Example profile selection:

```sh
cmake -S firmware -B build-generator \
  -DPICO_PWM_PROFILE=generator

cmake -S firmware -B build-monitor \
  -DPICO_PWM_PROFILE=monitor
```

The exact CMake option is part of the implementation work. The important
contract is that separate build directories produce separate, reproducible
firmware variants.

## Channel Table

Each logical channel is described by configuration rather than by a hard-coded
range. A channel entry should define at least:
| `id` | Stable host-visible logical channel number. |
| `backend` | Hardware PWM, PIO, software, or monitor backend. |
| `direction` | Input, output, or disabled. |
| `gpio` | Physical GPIO assigned to the channel. |
| `capabilities` | Operations supported by the channel. |
| `frequency_limits` | Valid requested or measurable frequency range. |

The default profile currently exposes 24 logical channels. The driver now uses
this table for logical-channel dispatch; the generator backend implementations
still retain their backend-local channel arrays. Monitor backends and alternate
profile selection remain follow-up work.

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
CLI](usb_cdc_cli.md) for the stable interactive interface.
