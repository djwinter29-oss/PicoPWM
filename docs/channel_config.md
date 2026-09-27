# Channel Configuration

PicoPWM has one firmware image and one logical channel table, owned entirely
by `firmware/src/pwmdriver/channel_config/channel_config.c`. There is no per-profile source file
and no `PICO_PWM_PROFILE` CMake option. Channel roles are chosen at runtime by
locking each of 3 fixed physical banks into a `generator` or `monitor` role;
see [Firmware Configuration](configuration.md#runtime-bank-locking) for the
host-facing model.

## Channel Table Ownership

`channel_config.c` owns:

- the runtime `pwm_profile_channels[PWM_PROFILE_CHANNEL_COUNT]` table, which
  starts fully `DISABLED` at boot
- the fixed GPIO assignment for each bank (`pwm_profile_bank_gpio`)
- `pwm_profile_lock_bank()`, which fills in one bank's 8 entries the first
  time that bank is locked, using the channel-entry macros in
  `firmware/src/pwmdriver/channel_config/channel_table.h`
- `pwm_profile_get_bank_state()`, `pwm_profile_bank_backend()`, and the shared
  lookup/validation helpers (`pwm_profile_get_channel`, `pwm_profile_get_gpio`,
  `pwm_profile_validate`, etc.)

Do not add channel-routing logic outside `channel_config.c`; that file
is the single source of truth for the channel table.

## Channel Entry

Each entry (built by the macros in `channel_table.h`) defines:

| Field | Requirement |
| --- | --- |
| `backend` | Generator or monitor backend implementation. |
| `direction` | `OUTPUT`, `INPUT`, or `DISABLED`. |
| `gpio` | Fixed per-bank GPIO (see below); ignored while `DISABLED`. |
| `backend_channel` | Local index accepted by the backend (0..7 within a bank). |
| `capabilities` | At least `READ` or `SET` as appropriate. |
| `min_frequency_hz` | Minimum generated or measurable nonzero frequency. |
| `max_frequency_hz` | Maximum generated or measurable frequency. |
| `accuracy_ppm` | Expected generation or measurement accuracy. |

The common validator uses these board-policy compile definitions by default:

- `PWM_PROFILE_GPIO_COUNT=30`
- `PWM_PROFILE_RESERVED_GPIO_MASK` reserving I2C1 GPIO `26/27` and LED GPIO `25`
- `PWM_PROFILE_REQUIRE_HW_CHANNEL_B=1`

A custom board target may override these definitions in its CMake branch when
its GPIO count, reserved pins, or hardware PWM pin policy differs.

## Fixed Bank GPIO Map

| Bank | Logical channels | GPIOs |
| --- | --- | --- |
| HW | 0..7 | `1, 3, 5, 7, 9, 11, 13, 15` (slice-B) |
| PIO | 8..15 | `0, 2, 4, 6, 8, 10, 12, 14` (companion slice-A) |
| SW | 16..23 | `16, 17, 18, 19, 20, 21, 22, 28` |

This map is fixed in `channel_config.c` and is not configurable per build; see
[Pinout](pinout.md) for the physical rationale (hardware PWM and PIO are each
restricted to one fixed set of 8 pins, not a free GPIO choice).

## Resource Rules

`pwm_profile_validate_table()` (and by extension every locked bank) enforces:

- GPIO ownership is unique.
- Input and output direction matches the backend.
- Hardware PWM channels are fixed to the 8 slice-B GPIOs; PIO channels are
  capped at 8 and fixed to the 8 companion slice-A GPIOs.
- Generator requests stay within the profile frequency envelope.
- Monitor limits describe the actual measurement capability.
- I2C register ranges still cover the advertised logical channel count.

GPIO23 and GPIO24 are optional software-only pins reserved for custom boards.
They are not part of the fixed SW bank above and must not be claimed by
hardware PWM or PIO entries.

## Adding a New Backend or Bank Layout

The fixed 3-bank, 8-pins-each layout is not expected to change for the
standard Pico board. If a custom board needs a different physical layout:

1. Add board-specific compile definitions (`PWM_PROFILE_GPIO_COUNT`,
   `PWM_PROFILE_RESERVED_GPIO_MASK`, `PWM_PROFILE_REQUIRE_HW_CHANNEL_B`) in a
   CMake branch, following the existing board-policy pattern.
2. If the bank/GPIO layout itself needs to change, update
   `pwm_profile_bank_gpio` and the bank-to-backend mapping in
   `pwm_profile_bank_backend()`/`pwm_profile_lock_bank()` — both stay inside
  `channel_config.c`.
3. Add or update firmware host-side tests in `firmware/tests/channel_config_test.c`
  and `firmware/tests/CMakeLists.txt`; run them through
  `tools/test/test-firmware-c.sh`.

All backend implementations are compiled once in the shared firmware target;
locking a bank selects which backend descriptor initializes and which logical
channels it owns. This should not require changes to `pwm_driver.c` or backend
source files unless the bank/backend mapping itself changes.

The USB shell and I2C protocol must remain unchanged when adding a bank
layout. Unsupported operations should return `PWM_DRIVER_RESULT_UNAVAILABLE`
through `device_api`.
