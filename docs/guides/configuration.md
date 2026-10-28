# Firmware Configuration

PicoPWM produces one firmware build from one stable logical channel interface.
Channel roles are selected once at startup by `pwm_driver_init()`; there is no
build-time profile selection or runtime role switching.

## Startup Bank Configuration

The GPIO map is fixed into three 8-pin banks (see [Hardware Setup](hardware-setup.md)):
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

The target record (`firmware/src/platform/boot_config.c`) contains a magic
value, format version, size, clock profile, role/address values, and a CRC-32.
It lives in the final 4 KiB flash sector. The linker script excludes that
sector from the program region, so an image that would overlap it fails to link. Power loss
during a save falls back to the defaults. Core 1 arms the flash
lockout victim before it reports ready. `config save` fails instead of erasing
flash when that victim is not armed, so Core 0 cannot block forever inside the
multicore lockout.

Each bank is configured independently, so `2^3 = 8` role combinations are
reachable in one firmware image. Each bank is pinned to one backend family: Bank A is HW, Bank B is PIO, and
Bank C is SW. Only the role (generator or monitor) is selectable; a request for
any other backend is rejected.

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

## CLI Configuration Commands

The CDC shell uses grouped commands. `config get` shows the running and target
bank settings. `config set` accepts one or more comma-separated assignments,
such as `config set banka=generator, clock=150`. It validates the complete
request, saves the new target configuration, and reboots the board to apply it.
The bank role, clock profile, and I2C address may be combined in one request.

The system clock accepts `default`, `150`, `200`, or `250` MHz. `default` keeps
the SDK clock: 125 MHz on Pico and 150 MHz on Pico 2. The I2C address is a
7-bit decimal value from 8 to 119.

Saving disables interrupts and locks the PWM core for the flash sector erase,
so USB and I2C stall until programming finishes or the 5-second watchdog resets
the board. Allow up to 2 seconds for the operation. For all command forms, see
the [USB CDC shell guide](usb_cdc_shell.md).

Use the logical channel number shown in the [USB CDC shell guide](usb_cdc_shell.md)
to read or set a channel. The assigned bank determines whether that channel
generates PWM or monitors an input; no backend selection is needed in the command.

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

See [Architecture](../design/architecture.md) for ownership boundaries and [USB CDC
shell](usb_cdc_shell.md) for the stable interactive interface. See
[PWM Driver Configuration](../design/pwm_driver_config.md) for the fixed Bank A/B/C GPIO map
and startup configuration validation rules.
