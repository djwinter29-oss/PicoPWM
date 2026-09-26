# Profile Authoring

A PicoPWM profile is a build-time channel table. The selected profile source is
compiled into the firmware; profiles are not switched at runtime.

## Profile Files

Profile tables live under:

```text
firmware/src/config/profiles/
  generator.c
  monitor.c
  software_generator.c
  software_monitor.c
```

CMake selects exactly one file through `PICO_PWM_PROFILE`:

```sh
cmake -S firmware -B build-generator \
  -DPICO_PWM_PROFILE=generator
```

The common implementation remains in
`firmware/src/config/pwm_profile.c`. A profile file should only define the
channel table:

```c
#include "config/profile_table.h"

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

The table must contain `PWM_PROFILE_CHANNEL_COUNT` entries. Logical IDs are
the array indices and must remain stable for host software.

## Resource Rules

A profile must validate:

- GPIO ownership is unique.
- Input and output direction matches the backend.
- Hardware PWM channels use compatible PWM slice/channel pins.
- PIO channels fit available PIO state machines and GPIO routing.
- Software channels fit the Core 1 timer, CPU, and interrupt budget.
- Generator requests stay within the profile frequency envelope.
- Monitor limits describe the actual measurement capability.
- I2C register ranges still cover the advertised logical channel count.

The default all-software profiles use GPIO `0..15` and `18..25`, leaving GPIO
`16` and `17` available for I2C. A different board or transport arrangement may
use a different map.

## Build Definitions

CMake supplies definitions needed by shared source code:

- `PICO_PWM_MONITOR_PROFILE` for monitor profiles
- `PICO_PWM_SOFTWARE_PROFILE` for all-software profiles

Use these only when the backend implementation or channel storage layout must
change. The profile table itself should describe behavior; shared code should
not inspect a profile name to route individual channels.

## Adding a Profile

1. Add a new table under `firmware/src/config/profiles/`.
2. Add one `PICO_PWM_PROFILE` branch in `firmware/CMakeLists.txt`.
3. Select the required backend sources and PIO program, if any.
4. Add compile definitions only when shared storage or backend compilation
   requires them.
5. Add a profile metadata test to `tools/test/cli-shell-test.sh`.
6. Document the GPIO map, backend limits, and expected accuracy.
7. Build the profile in a separate build directory and run the host tests.

The USB shell and I2C protocol must remain unchanged when adding a profile.
Unsupported operations should return `PWM_DRIVER_RESULT_UNAVAILABLE` through
`control_iface`.
