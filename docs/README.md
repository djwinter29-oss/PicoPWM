# Documentation

PicoPWM documentation is grouped by audience and purpose, following the
`guides/` and `design/` split used by PicoUART and PicoForge.

## Guides

Task-oriented information for building, configuring, connecting, and operating
PicoPWM:

- [Firmware Configuration](guides/configuration.md)
- [Pinout](guides/pinout.md)
- [Control Interfaces](guides/control/README.md)
- [USB CDC CLI](guides/control/usb_cdc_cli.md)
- [I2C Protocol](guides/control/i2c_protocol.md)

## Design

Architecture and implementation details for contributors:

- [Architecture](design/architecture.md)
- [PWM Driver Configuration](design/pwm_driver_config.md)
- [PWM Driver Design](design/pwm_driver_design.md)
- [PIO PWM Design](design/pio_pwm_design.md)
- [Generator designs](design/generator/)
- [Monitor designs](design/monitor/)
- [CDC/HID Overview](design/usb/cdc-hid-overview.md) and
  [HID Report Reference](design/usb/hid-report-reference.md)
