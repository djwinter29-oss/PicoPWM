# Documentation

PicoPWM documentation is grouped by audience and purpose, following the
`guides/` and `design/` split used by PicoUART and PicoForge.

## Guides

Task-oriented information for building, configuring, connecting, and operating
PicoPWM:

- [Hardware Setup](guides/hardware-setup.md)
- [USB CDC shell](guides/usb_cdc_shell.md)
- [Setup and Troubleshooting](guides/setup-and-troubleshooting.md)

## Deployment

Release packaging and publishing workflows:

- [Builds and Publishing](deployment/releases.md)

## Design

Architecture and implementation details for contributors:

- [Architecture](design/architecture.md)
- [PWM Driver Design](design/pwm-driver/driver-design.md)
- [PWM Driver Configuration](design/pwm-driver/configuration.md)
- [Hardware PWM](design/pwm-driver/hardware.md) — generator and monitor
- [PIO PWM](design/pwm-driver/pio.md) — generator and monitor
- [Software PWM](design/pwm-driver/software.md) — generator and monitor
- [I2C Protocol Reference](design/i2c/i2c-protocol.md)
- [USB Interface Overview](design/usb/usb-interface-overview.md) and
  [HID Report Reference](design/usb/hid-report-reference.md)

## Tests

Hardware setup and recorded validation results:

- [HIL Test Connections](tests/hil-test-connection.md)
- [HIL Test Plan](tests/hil-test-plan.md)
- [HIL Test Records](tests/records/README.md)
