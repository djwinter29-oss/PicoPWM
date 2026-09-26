# Profile Authoring

A PicoPWM profile is a build-time channel table. The selected profile source is
compiled into the firmware; profiles are not switched at runtime.

## Profile Files

Profile tables live under:

```text
firmware/src/profile/profiles/
  generator.c
  monitor.c
  pio_sw_gen_sw_mon.c
```

CMake selects exactly one file through `PICO_PWM_PROFILE`:

```sh
cmake -S firmware -B build-generator \
  -DPICO_PWM_PROFILE=generator
```

The common implementation remains in
`firmware/src/profile/pwm_profile.c`. A profile file should only define the
channel table:

```c
#include "profile/profile_table.h"

const pwm_profile_channel_t pwm_profile_channels[PWM_PROFILE_CHANNEL_COUNT] = {
    /* one entry per logical channel */
};
```

Do not add profile-specific routing logic to `pwm_profile.c`; that file owns
lookup, frequency validation, backend names, and shared profile helpers.

## Channel Entry

Each entry must define:

| Field | Requirement |
| --- | --- |
| `backend` | Generator or monitor backend implementation. |
| `direction` | `OUTPUT`, `INPUT`, or `DISABLED`. |
| `gpio` | A valid, uniquely owned GPIO for the selected board/profile. |
| `backend_channel` | Local index accepted by the backend. |
| `capabilities` | At least `READ` or `SET` as appropriate. |
| `min_frequency_hz` | Minimum generated or measurable nonzero frequency. |
| `max_frequency_hz` | Maximum generated or measurable frequency. |
| `accuracy_ppm` | Expected generation or measurement accuracy. |

The common validator uses these board-policy compile definitions by default:

- `PWM_PROFILE_GPIO_COUNT=30`
- `PWM_PROFILE_RESERVED_GPIO_MASK` reserving I2C1 GPIO `26/27` and LED GPIO `25`
- `PWM_PROFILE_REQUIRE_HW_CHANNEL_B=1`

A custom board profile may override these definitions in its CMake branch when
its GPIO count, reserved pins, or hardware PWM pin policy differs.

The table must contain `PWM_PROFILE_CHANNEL_COUNT` entries. Logical IDs are
the array indices and must remain stable for host software.

## Resource Rules

A profile must validate:

- GPIO ownership is unique.
- Input and output direction matches the backend.
- Hardware PWM channels are fixed to the 8 slice-B GPIOs; this is one fixed pin
  set of 8, not a free choice among compatible slice/channel pins.
- PIO channels are capped at 8 and fixed to the 8 companion slice-A GPIOs paired
  with the hardware PWM slices.
- Software channels may use any GPIO not claimed by hardware PWM or PIO, fit to
  the Core 1 timer, CPU, and interrupt budget.
- Generator requests stay within the profile frequency envelope.
- Monitor limits describe the actual measurement capability.
- I2C register ranges still cover the advertised logical channel count.
- board-specific reserved pins and connector availability are respected by the
  selected profile and CMake board configuration.

The default `generator`/`monitor` profiles use GPIO `0..22` and `28` across
the hardware, PIO, and software banks, leaving GPIO `26/27` for I2C1 and
GPIO24 for the standard Pico VBUS sense function, and GPIO25 for the board
LED. A different board or transport arrangement may use a different map.

GPIO23 and GPIO24 are optional software-only pins in the profile model. They
are not assigned by normal Pico profiles and must not be claimed by hardware
PWM or PIO entries. A custom board/profile may assign them to software
generator or software monitor channels when those pins are physically
available and the board policy permits them.

## Adding a Profile

1. Add a new table under `firmware/src/profile/profiles/`.
2. Add one `PICO_PWM_PROFILE` branch in `firmware/CMakeLists.txt`.
3. Add or update backend resource requirements only if the common backend set
  cannot support the new table.
4. Add a profile metadata test to `tools/test/cli-shell-test.sh`.
5. Document the GPIO map, backend limits, and expected accuracy.
6. Build the profile in a separate build directory and run the host tests.

All backend implementations are compiled once in the shared firmware target;
the profile table selects which backend descriptors initialize and which
logical channels they own. Adding an ordinary profile should not require
changes to `pwm_driver.c` or backend source files.

The USB shell and I2C protocol must remain unchanged when adding a profile.
Unsupported operations should return `PWM_DRIVER_RESULT_UNAVAILABLE` through
`control_iface`.

The built-in `mixed` profile is an example of a heterogeneous table: it uses
PIO generation, software generation, and software monitoring in one image.
Its GPIO map is board-specific and demonstrates why profile validation must
reject unavailable or conflicting pins before Core 1 starts.
