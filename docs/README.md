# Documentation

For flashing and control options, start with the [project README](../README.md).

## Use PicoPWM

| Task | Start here |
| --- | --- |
| Wire a board or find a channel's GPIO | [Hardware setup](guides/hardware-setup.md) |
| Generate your first signal | [First generator output](guides/configuration.md#first-generator-output) |
| Set roles, clock, or address; check operating ranges | [Configuration](guides/configuration.md) |
| Install or connect a dashboard | [Browser](../host/direct/README.md), [Python](../host/python/README.md), or [.NET](../host/dotnet/README.md) |
| Edit outputs or save dashboard settings | [Dashboard controls](guides/dashboard-controls.md) |
| Fix Linux USB permissions | [Linux HID access](guides/linux-hid-access.md) |
| Diagnose a problem or recover a board | [Troubleshooting](guides/troubleshooting.md) |

## Wire Protocols

| Interface | Use it for | Reference |
| --- | --- | --- |
| USB CDC | Interactive text commands | [CDC shell](protocols/usb-cdc.md) |
| USB HID | Structured status and actions from host software | [USB HID reports](protocols/usb-hid.md) |
| I2C | Channel control from an external controller | [I2C protocol](protocols/i2c.md) |

To script a board from Python, use the
[Python client](../host/python/README.md#use-from-python), which talks USB HID directly.

## Firmware Design

- [Architecture](design/architecture.md) — cores, request path, and transports
- [PWM design](design/pwm.md) — multicore boundary, banks, and backend timing

## Development

- [Contributing](../CONTRIBUTING.md) — firmware builds, tests, and pull requests
- [Host app development](development/host-apps.md) — tests, launchers, and packaging checks
- [Dashboard backend API](development/dashboard-api.md) — Python/.NET HTTP contract
- [CI and releases](development/ci-and-releases.md) — workflows and publishing

## Validation

- [CDC loopback runner](../tools/hil/README.md) — setup, execution, and cleanup
- [Native HID qualification](validation/native-hid-qualification.md) — real-device transport checks
- Hardware tests: [fixture](validation/hil/connections.md),
  [test plan](validation/hil/test-plan.md), [records](validation/hil/records/README.md)
