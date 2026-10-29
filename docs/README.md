# Documentation

PicoPWM documentation is grouped by audience and purpose: guides cover using
the device, design documents explain its implementation and interfaces,
deployment documents cover releases, and test documents describe validation.

## Guides

Task-oriented information for configuring, connecting, and operating PicoPWM:

- [Hardware Setup](guides/hardware-setup.md)
- [USB CDC shell](guides/usb-cdc-shell.md)
- [Setup and Troubleshooting](guides/setup-and-troubleshooting.md)

## Deployment

Release packaging and publishing workflows:

- [Builds and Publishing](deployment/releases.md)

## Design and Protocol References

Architecture and implementation details for contributors, plus interface
contracts for host implementers:

- [Architecture](design/architecture.md)
- [PWM Driver Design](design/pwm-driver/driver-design.md)
- [PWM Driver Configuration](design/pwm-driver/configuration.md)
- [Hardware PWM](design/pwm-driver/hardware.md) — generator and monitor
- [PIO PWM](design/pwm-driver/pio.md) — generator and monitor
- [Software PWM](design/pwm-driver/software.md) — generator and monitor
- [I2C Slave Driver Design](design/i2c/i2c-slave-driver-design.md) — generic transport and protocol-layer ownership
- [I2C Protocol Reference](design/i2c/i2c-protocol.md)
- [USB Interface Overview](design/usb/usb-interface-overview.md) and
  [HID Report Reference](design/usb/hid-report-reference.md)

## Tests

Hardware setup and recorded validation results:

- [HIL Test Connections](tests/hil-test-connections.md)
- [HIL Test Plan](tests/hil-test-plan.md)
- [HIL Test Records](tests/records/README.md)
