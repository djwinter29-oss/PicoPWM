# PWM Driver Configuration

PicoPWM has one firmware image and one logical channel table, owned entirely
by `firmware/src/driver/pwm/pwm_driver.c`. Persisted settings (bank roles and I2C
address, system clock profile, one CRC-protected flash record) live in `firmware/src/platform/boot_config.c`
and are handed to the driver as three roles. There is no per-profile source file
and no `PICO_PWM_PROFILE` CMake option. Channel roles are stored in the startup
configuration. The host edits that target, saves it, and applies it by rebooting.
See [Firmware Configuration](../guides/configuration.md#startup-bank-configuration) for the
host-facing model.

## Channel Resolution

`pwm_driver.c` owns:

- the per-bank role recorded by `pwm_driver_init()` (validated earlier by `boot_config`)
- the per-channel lookups (`pwm_driver_config_channel_role`, `pwm_driver_config_channel_name`),
  and the frequency limits (`pwm_driver_config_frequency_supported`)

Each bank is wired to one module: A=`hardware`, B=`pio`, C=`software`. The module reads its
GPIOs straight from its `board.c` const (`hardware_pwm_config`, `pio_pwm_config`,
`software_pwm_config`). `pwm_driver.c` initializes the generator or monitor variant once at
boot according to the bank role and routes `channel / 8` (bank) and `channel % 8` (index) to
it. A monitor module has no `set`, so writes are refused. There is no stored channel table.

## Fixed Bank GPIO Map and Backend Rules

| Bank | Logical channels | GPIOs |
| --- | --- | --- |
| Bank A | 0..7 | `1, 3, 5, 7, 9, 11, 13, 15` (slice-B); HW backend |
| Bank B | 8..15 | `0, 2, 4, 6, 8, 10, 12, 14` (companion slice-A); PIO backend |
| Bank C | 16..23 | `16, 17, 18, 19, 20, 21, 22, 28`; SW backend only |

This map is defined in `config/board.c` and is not configurable at runtime; see
[Hardware Setup](../guides/hardware-setup.md) for the physical rationale.

## Resource Rules

The pin map is fixed in `config/board.c` and is not checked at runtime. Hardware PWM uses
the 8 slice-B GPIOs and PIO uses the 8 companion slice-A GPIOs. GPIO23 and GPIO24 are
optional software-only pins reserved for custom boards and must not be claimed by hardware
PWM or PIO entries.

## Adding a New Backend or Bank Layout

The fixed 3-bank, 8-pins-each layout is not expected to change for the
standard Pico board. If a custom board needs a different physical layout:

1. If the bank/GPIO layout itself needs to change, update the
  `board.c` consts and the bank-to-backend mapping in
  `pwm_driver_backends[][]`.
2. Add or update firmware host-side tests in `firmware/tests/` and `firmware/tests/CMakeLists.txt`; run them through
  `tools/test/test-firmware-c.sh`.

All backend implementations are compiled once in the shared firmware target.
The startup configuration selects which backend descriptor initializes and which logical
channels it owns. This should not require changes to `pwm_driver.c` or backend
source files unless the bank/backend mapping itself changes.

The USB shell and I2C protocol must remain unchanged when adding a bank
layout. Unsupported operations should return `PWM_DRIVER_RESULT_UNAVAILABLE`
through `device`.
