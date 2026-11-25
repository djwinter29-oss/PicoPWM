# Documentation

Start with the root [README](../README.md) for the product overview and quick
start. Firmware build and test commands are in [CONTRIBUTING](../CONTRIBUTING.md).

The linked pages are the detailed references for their subjects. Other pages
should summarize only what their audience needs and link here for full details.

| Folder | Read it when you want to… |
| --- | --- |
| [guides/](guides/) | Wire, configure, operate, or troubleshoot a board |
| [protocols/](protocols/) | Look up an interface contract: CDC commands, HID reports, I2C, or HTTP |
| [design/](design/) | Change firmware and need its ownership rules and tradeoffs |
| [development/](development/) | Build, test, or release the host applications |
| [validation/](validation/) | Qualify a build on real hardware |

## Use PicoPWM

- [Hardware setup](guides/hardware-setup.md) — board compatibility, wiring, and the channel-to-GPIO map
- [Configuration](guides/configuration.md) — bank roles, clock profiles, and I2C address
- [CDC shell](protocols/usb-cdc.md) — interactive serial commands (full command reference)
- [Linux HID access](guides/linux-hid-access.md) — device permissions for host applications
- [Troubleshooting](guides/troubleshooting.md) — connection problems, unexpected output, and recovery

Host applications are documented beside their code:
[Direct Dashboard](../host/direct/README.md) (browser, WebHID),
[Python dashboard and HID client](../host/python/README.md), and
[.NET dashboard](../host/dotnet/README.md).

## Interface Contracts

| Interface | Use it for | Reference |
| --- | --- | --- |
| USB CDC | Interactive text commands | [CDC shell](protocols/usb-cdc.md) |
| USB HID | Structured status and actions from host software | [USB HID reports](protocols/usb-hid.md) |
| I2C | Channel control from an external controller | [I2C protocol](protocols/i2c.md) |
| HTTP | Scripting the Python or .NET dashboard | [Dashboard HTTP API](protocols/dashboard-http-api.md) |

## Firmware Design

- [Architecture](design/architecture.md) — core ownership and the request path; read first
- [CDC shell design](design/usb-cdc.md) — command handling and bounded I/O
- [I2C slave design](design/i2c.md) — bus transport and deferred commands
- PWM:
  - [Driver](design/pwm/driver.md) — the multicore boundary and realized-state readback
  - [Banks](design/pwm/banks.md) — fixed banks, startup roles, and resource ownership
  - Backends, including timing and measurement limits:
    [hardware PWM](design/pwm/backend-hardware.md) (Bank A),
    [PIO](design/pwm/backend-pio.md) (Bank B), and
    [software](design/pwm/backend-software.md) (Bank C)

## Development

- [Host app development](development/host-apps.md) — tests, launchers, and packaging checks
- [CI and releases](development/ci-and-releases.md) — workflows and publishing

## Validation

- [Native HID qualification](validation/native-hid-qualification.md) — host app checks against a real board
- Hardware-in-the-loop (HIL):
  - [Fixture connections](validation/hil/connections.md)
  - [Test plan](validation/hil/test-plan.md)
  - [Test records](validation/hil/records/README.md)
