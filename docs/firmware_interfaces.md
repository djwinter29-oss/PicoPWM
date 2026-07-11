# Firmware Interfaces

This page documents the current source-level interface layout under `firmware/src/`.

Use this page as a reference for public or semi-public firmware interfaces.

If you need the design narrative first, read these pages before this one:

1. [Architecture](architecture.md)
2. [Control Protocol](protocol.md)
3. [PWM Driver Design](detail/pwm_driver_design.md)

## Reference Scope

This page covers the current interface surface for:

- transport-facing helpers used on Core 0
- the shared `control_iface` layer
- the `pwmdriver` public API and shared types
- small board utility helpers used by command paths

It does not repeat the full Core 0/Core 1 architecture story or mailbox flow details.

## Transport-Facing APIs

### `usb/usb_cdc.h`

TinyUSB CDC byte transport used by the CLI.

```c
void usb_cdc_init(void);
void usb_cdc_poll(void);
bool usb_cdc_is_connected(void);
uint32_t usb_cdc_read(uint8_t *data, uint32_t capacity);
bool usb_cdc_write(const uint8_t *data, uint32_t length);
```

Responsibilities:

- initialize TinyUSB and local RX/TX queues
- service TinyUSB background work
- provide byte-oriented read/write helpers to the CLI shell

### `cli/device_cli.h`

User-facing command layer on top of the generic shell.

```c
void device_cli_init(const cli_shell_transport_t *transport);
void device_cli_on_connected(void);
void device_cli_poll(void);
```

Responsibilities:

- register human-readable CLI commands such as `get`, `set`, `status`, `led`, `reboot`, and `stop`
- print help when the CDC connection is first opened
- translate command lines into `control_iface` operations

### `i2c/i2c_control_map.h`

I2C register protocol helper above the shared control layer.

```c
typedef enum {
    I2C_CONTROL_MAP_REG_INFO = 0x00u,
    I2C_CONTROL_MAP_REG_VERSION = 0x01u,
    I2C_CONTROL_MAP_REG_CHANNEL_COUNT = 0x02u,
    I2C_CONTROL_MAP_REG_CH_BASE = 0x10u,
    I2C_CONTROL_MAP_REG_SET_BASE = 0x30u,
    I2C_CONTROL_MAP_REG_SET_FREQ_BASE = 0x50u,
    I2C_CONTROL_MAP_REG_SET_DUTY_BASE = 0x70u,
    I2C_CONTROL_MAP_REG_STOP_ALL = 0x90u,
    I2C_CONTROL_MAP_REG_LED = 0x91u,
    I2C_CONTROL_MAP_REG_REBOOT = 0x92u,
} i2c_control_map_reg_t;

bool i2c_control_map_is_write_register(uint8_t reg);
uint8_t i2c_control_map_expected_write_length(uint8_t reg);
bool i2c_control_map_read_register(uint8_t reg, uint8_t last_status, uint8_t *response, uint8_t *response_len);
pwm_driver_result_t i2c_control_map_execute_write(uint8_t reg, const uint8_t *payload, uint8_t payload_len);
```

Responsibilities:

- define the binary register map
- translate read registers into response payloads
- translate deferred I2C write payloads into `control_iface`, LED, or reboot operations

### `i2c/i2c_slave.h`

I2C slave transport implementation for address `0x40`.

Key behavior:

- captures request bytes in the ISR
- prepares read responses for the master
- defers write execution into `i2c_slave_poll()` on Core 0

## Shared Control API

### `control/control_iface.h`

This is the preferred transport-neutral API for Core 0 command layers.

```c
const char *control_iface_device_name(void);
const char *control_iface_firmware_version(void);
uint8_t control_iface_channel_count(void);
bool control_iface_get_channel(uint channel, pwm_driver_state_t *state);
pwm_driver_result_t control_iface_set_channel(uint channel, uint32_t freq_hz, uint8_t duty);
pwm_driver_result_t control_iface_set_channel_freq(uint channel, uint32_t freq_hz);
pwm_driver_result_t control_iface_set_channel_duty(uint channel, uint8_t duty);
pwm_driver_result_t control_iface_stop_all(void);
```

Use this layer when you are writing transport code on Core 0.

Why it exists:

- CDC CLI and I2C need the same logical operations
- device info and firmware version should not be duplicated per transport
- realized channel reads should come from one path
- transport code should not know `pwmdriver` internals

## `pwmdriver/pwm_driver.h`

`pwmdriver` owns the multicore mutation boundary and the realized-state snapshot.

### Constants

| Constant | Meaning |
|----------|---------|
| `HW_PWM_COUNT` | Hardware PWM logical channel count |
| `PIO_PWM_DRIVER_COUNT` | PIO PWM logical channel count |
| `SW_PWM_COUNT` | Software PWM logical channel count |
| `HW_PWM_CHANNEL_BASE` | Hardware channel base |
| `PIO_PWM_CHANNEL_BASE` | PIO channel base |
| `SW_PWM_CHANNEL_BASE` | Software channel base |
| `PWM_DRIVER_CHANNEL_COUNT` | Total logical channel count |

### Shared Types

```c
typedef enum {
    PWM_DRIVER_RESULT_OK = 0,
    PWM_DRIVER_RESULT_BUSY,
    PWM_DRIVER_RESULT_INVALID,
    PWM_DRIVER_RESULT_UNAVAILABLE,
    PWM_DRIVER_RESULT_TIMEOUT,
    PWM_DRIVER_RESULT_APPLY_FAILED,
} pwm_driver_result_t;

typedef struct {
    uint32_t freq_hz;
    uint8_t duty;
    uint32_t pulse_count;
} pwm_driver_state_t;
```

### High-Level Logical Helpers

These helpers are still declared in `pwm_driver.h` and are used by `control_iface` internally:

```c
pwm_driver_result_t control_set(uint channel, uint32_t freq_hz, uint8_t duty);
pwm_driver_result_t control_set_freq(uint channel, uint32_t freq_hz);
pwm_driver_result_t control_set_duty(uint channel, uint8_t duty);
bool control_get(uint channel, pwm_driver_state_t *state);
uint32_t control_get_freq(uint channel);
uint8_t control_get_duty(uint channel);
uint32_t control_get_pulse_count(uint channel);
bool control_is_enabled(uint channel);
pwm_driver_result_t control_stop_all(void);
```

These helpers:

- operate on logical channel numbers
- preserve read-modify-write behavior where needed
- participate in the Core 0 public-write serialization path

### Cross-Core Public API

```c
void pwm_driver_launch(void);
bool pwm_driver_is_ready(void);
pwm_driver_result_t pwm_driver_set_freq(uint channel, uint32_t freq_hz, uint8_t duty);
bool pwm_driver_get(uint channel, pwm_driver_state_t *state);
```

Notes:

- `freq_hz` is now an integer-Hz API surface.
- `duty` is now an integer percent in the range `0..100`; higher write values are clamped to `100`.

Intent:

- `pwm_driver_launch()` starts Core 1 backend ownership
- `pwm_driver_is_ready()` gates command acceptance
- `pwm_driver_set_freq()` is the command-ingress mutation path from Core 0
- `pwm_driver_get()` returns the latest published realized state

For transport-facing code, prefer `control_iface` over calling these entry points directly unless you are working on the shared control layer itself.

## Board Utility APIs

### `driver/led.h`

```c
void led_init(void);
void led_set(bool on);
```

Used by the CLI and I2C control map to expose board LED control.

### `driver/system.h`

```c
void system_init_clock(void);
void system_reboot(void);
```

`system_reboot()` is exposed through both the CLI and I2C command paths.

## Backend Headers

The following headers are Core 1 implementation details and should not be used by higher command layers directly:

- `pwmdriver/hw_pwm_driver.h`
- `pwmdriver/pio/generator.h`
- `pwmdriver/pwm_driver_internal.h`
- `pwmdriver/sw/generator.h`
- `pwmdriver/sw/monitor.h`

They own backend-local setup, timing, and publish helpers. See [detail/pwm_driver_design.md](detail/pwm_driver_design.md) for the detailed design.

## Related Design Pages

- [Architecture](architecture.md) — system structure, ownership, and request flow
- [Control Protocol](protocol.md) — USB CLI syntax and I2C register behavior
- [PWM Driver Design](detail/pwm_driver_design.md) — mailbox flow, publication model, and backend internals
