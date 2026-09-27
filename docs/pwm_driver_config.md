# PWM Driver Configuration

PicoPWM has one firmware image and one logical channel table, owned entirely
by `firmware/src/pwmdriver/pwm_driver_config.c`. There is no per-profile source file
and no `PICO_PWM_PROFILE` CMake option. Channel roles are chosen at runtime by
locking each of 3 fixed physical banks into a `generator` or `monitor` role;
see [Firmware Configuration](configuration.md#startup-bank-configuration) for the
host-facing model.

## Channel Table Ownership

`pwm_driver_config.c` owns:

- the runtime `pwm_driver_config_channels[PWM_DRIVER_CONFIG_CHANNEL_COUNT]` table, which
  starts fully `DISABLED` at boot
- the fixed GPIO assignment for each bank (`pwm_driver_config_bank_gpio`)
- `pwm_driver_config_configure()`, which fills in all bank entries from the
  startup roles, using the channel-entry macros in
  `firmware/src/pwmdriver/pwm_driver_table.h`
- the shared lookup/validation helpers (`pwm_driver_config_get_channel`, `pwm_driver_config_get_gpio`,
  `pwm_driver_config_validate`, etc.)

Do not add channel-routing or persistence logic outside `pwm_driver_config.c`; that file
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

- `PWM_DRIVER_CONFIG_GPIO_COUNT=30`
- `PWM_DRIVER_CONFIG_RESERVED_GPIO_MASK` reserving I2C1 GPIO `26/27` and LED GPIO `25`
- `PWM_DRIVER_CONFIG_REQUIRE_HW_CHANNEL_B=1`

A custom board target may override these definitions in its CMake branch when
its GPIO count, reserved pins, or hardware PWM pin policy differs.

## Fixed Bank GPIO Map and Backend Rules

| Bank | Logical channels | GPIOs |
| --- | --- | --- |
| Bank A | 0..7 | `1, 3, 5, 7, 9, 11, 13, 15` (slice-B); HW or SW backend |
| Bank B | 8..15 | `0, 2, 4, 6, 8, 10, 12, 14` (companion slice-A); PIO or SW backend |
| Bank C | 16..23 | `16, 17, 18, 19, 20, 21, 22, 28`; SW backend only |

This map is fixed in `pwm_driver_config.c` and is not configurable at runtime; see
[Pinout](pinout.md) for the physical rationale.

## Resource Rules

`pwm_driver_config_validate_table()` (and by extension every locked bank) enforces:

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

1. Add board-specific compile definitions (`PWM_DRIVER_CONFIG_GPIO_COUNT`,
   `PWM_DRIVER_CONFIG_RESERVED_GPIO_MASK`, `PWM_DRIVER_CONFIG_REQUIRE_HW_CHANNEL_B`) in a
   CMake branch, following the existing board-policy pattern.
2. If the bank/GPIO layout itself needs to change, update
  `pwm_driver_config_bank_gpio` and the bank-to-backend mapping in
  `pwm_driver_config_fill_bank()`/`pwm_driver_config_configure()` — both stay inside
  `channel_config.c`.
3. Add or update firmware host-side tests in `firmware/tests/pwm_driver_config_test.c`
  and `firmware/tests/CMakeLists.txt`; run them through
  `tools/test/test-firmware-c.sh`.

All backend implementations are compiled once in the shared firmware target;
locking a bank selects which backend descriptor initializes and which logical
channels it owns. This should not require changes to `pwm_driver.c` or backend
source files unless the bank/backend mapping itself changes.

The USB shell and I2C protocol must remain unchanged when adding a bank
layout. Unsupported operations should return `PWM_DRIVER_RESULT_UNAVAILABLE`
through `device_api`.
