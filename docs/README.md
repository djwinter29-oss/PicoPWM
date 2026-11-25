# Documentation

Start with the root [README](../README.md) for product overview and quick start.

| Folder | Audience | Contents |
| --- | --- | --- |
| [guides/](guides/) | Users | Wiring, configuration, CDC shell, troubleshooting |
| [protocols/](protocols/) | Host and controller integrators | I2C, USB HID, and dashboard HTTP contracts |
| [design/](design/) | Firmware contributors | Architecture, ownership, and backend tradeoffs |
| [development/](development/) | Host-app contributors and maintainers | Host-app tests, builds, and releases |
| [validation/](validation/) | Hardware testers | Native HID qualification and HIL testing |

Firmware build and test commands are in [CONTRIBUTING](../CONTRIBUTING.md).

## Use PicoPWM

- [Hardware setup](guides/hardware-setup.md) — board compatibility, wiring, and channel map
- [Configuration](guides/configuration.md) — bank roles, clock profiles, and I2C address
- [USB CDC shell](guides/usb-cdc-shell.md) — command syntax and interactive use
- [Linux HID access](guides/linux-hid-access.md) — device permissions for host applications
- [Troubleshooting](guides/troubleshooting.md) — connection problems, unexpected output, and recovery

Host applications are documented beside their code:
[Direct Dashboard](../host/direct/README.md) (WebHID),
[Python dashboard and HID client](../host/python/README.md), and
[.NET dashboard](../host/dotnet/README.md).

## Interface Contracts

- [I2C protocol](protocols/i2c.md)
- [USB interface overview](protocols/usb/interface-overview.md)
- [HID report reference](protocols/usb/hid-report-reference.md)
- [Dashboard HTTP API](protocols/dashboard-http-api.md) — shared by the Python and .NET dashboards

## Firmware Design

- [Runtime architecture](design/architecture.md)
- [USB CDC CLI](design/cdc-cli.md)
- [I2C slave driver](design/i2c-slave-driver.md)
- PWM driver: [driver design](design/pwm/driver.md), [configuration](design/pwm/configuration.md),
  and the [hardware PWM](design/pwm/hardware.md), [PIO](design/pwm/pio.md), and
  [software](design/pwm/software.md) backends

## Development

- [Host app development](development/host-apps.md) — tests, launchers, and packaging checks
- [Builds and publishing](development/releases.md) — CI workflows and releases

## Validation

- [Native HID qualification](validation/native-hid-qualification.md)
- [HIL connection guide](validation/hil/connections.md)
- [HIL test plan](validation/hil/test-plan.md)
- [HIL test records](validation/hil/records/README.md)

## Sources of Truth

When a summary conflicts with a reference, use the authoritative page below and
update the summary:

| Subject | Authoritative reference |
| --- | --- |
| Physical channel and GPIO map | [Hardware setup](guides/hardware-setup.md) |
| Bank roles and user configuration | [Configuration](guides/configuration.md) |
| CDC command syntax and behavior | [USB CDC shell](guides/usb-cdc-shell.md) |
| I2C wire and register contract | [I2C protocol](protocols/i2c.md) |
| HID report layouts and actions | [HID report reference](protocols/usb/hid-report-reference.md) |
| Dashboard routes and payloads | [Dashboard HTTP API](protocols/dashboard-http-api.md) |
| PWM timing and measurement limits | [Hardware PWM](design/pwm/hardware.md), [PIO](design/pwm/pio.md), and [Software PWM](design/pwm/software.md) |
| HIL fixture and execution | [HIL connection guide](validation/hil/connections.md) and [HIL test plan](validation/hil/test-plan.md) |
