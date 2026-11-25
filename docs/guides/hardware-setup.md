# Hardware Setup

This guide covers wiring and the fixed physical mapping for PicoPWM firmware on
Raspberry Pi Pico and Pico 2 boards. The static pin sets live in
`firmware/src/config/board.c`; the logical channel IDs permanently identify one
of three 8-channel banks.

## Before Wiring

All GPIO signals are 3.3 V only; never apply 5 V to a channel pin. Connect the
Pico ground to the ground of any connected signal source or measurement
instrument. Never connect generator outputs together.

The standard profiles use fixed channel-to-GPIO assignments. Custom board
profile constraints are described in the [PWM bank design](../design/pwm/banks.md).

## Board Compatibility

The `pico` and `pico2` profiles use the standard Pico and Pico 2 GPIO assignments.
Pico W and Pico 2 W can use the corresponding profiles; wireless functionality
is not enabled, and onboard LED control is unavailable on those boards.

A third-party RP2040/RP2350 board is not automatically compatible: verify it
exposes channel GPIOs `0-22` and `28`; I2C additionally requires GPIOs `26` and
`27`. The standard profile assumes the LED is on GPIO 25. Verify SWD access
only if programming with a Debug Probe.

## PWM Channels

The standard profile has 24 channels in three fixed banks. Each bank has one
backend; startup configuration selects whether it generates or monitors PWM.
This table is the complete logical-channel-to-GPIO mapping.

| Logical Channel | Backend | Backend-local Channel | GPIO | Notes |
|-----------------|---------|-----------------------|------|-------|
| 0 | Hardware PWM | 0 | GPIO 1 | PWM slice 0, channel B |
| 1 | Hardware PWM | 1 | GPIO 3 | PWM slice 1, channel B |
| 2 | Hardware PWM | 2 | GPIO 5 | PWM slice 2, channel B |
| 3 | Hardware PWM | 3 | GPIO 7 | PWM slice 3, channel B |
| 4 | Hardware PWM | 4 | GPIO 9 | PWM slice 4, channel B |
| 5 | Hardware PWM | 5 | GPIO 11 | PWM slice 5, channel B |
| 6 | Hardware PWM | 6 | GPIO 13 | PWM slice 6, channel B |
| 7 | Hardware PWM | 7 | GPIO 15 | PWM slice 7, channel B |
| 8 | PIO PWM | 0 | GPIO 0 | Companion pin to hardware channel 0 |
| 9 | PIO PWM | 1 | GPIO 2 | Companion pin to hardware channel 1 |
| 10 | PIO PWM | 2 | GPIO 4 | Companion pin to hardware channel 2 |
| 11 | PIO PWM | 3 | GPIO 6 | Companion pin to hardware channel 3 |
| 12 | PIO PWM | 4 | GPIO 8 | Companion pin to hardware channel 4 |
| 13 | PIO PWM | 5 | GPIO 10 | Companion pin to hardware channel 5 |
| 14 | PIO PWM | 6 | GPIO 12 | Companion pin to hardware channel 6 |
| 15 | PIO PWM | 7 | GPIO 14 | Companion pin to hardware channel 7 |
| 16 | Software PWM | 0 | GPIO 16 | |
| 17 | Software PWM | 1 | GPIO 17 | |
| 18 | Software PWM | 2 | GPIO 18 | |
| 19 | Software PWM | 3 | GPIO 19 | |
| 20 | Software PWM | 4 | GPIO 20 | |
| 21 | Software PWM | 5 | GPIO 21 | |
| 22 | Software PWM | 6 | GPIO 22 | |
| 23 | Software PWM | 7 | GPIO 28 | Shared with ADC2 |

## I2C

The I2C control interface uses I2C1:

| Signal | GPIO |
|--------|------|
| SDA | GPIO 26 |
| SCL | GPIO 27 |

The default 7-bit I2C address is `0x40`; it can be changed in configuration.
External pull-up resistors are required for reliable bus operation; 4.7 kOhm
is typical. The MCU's internal pull-ups do not replace external pull-ups. Use
a 100 kHz or 400 kHz controller clock; see
the [I2C protocol reference](../protocols/i2c.md) for bus and register details.

## Shared Pins

- GPIO 25 is reserved for the on-board LED and is not assigned to a default PWM channel.
- GPIO 24 is reserved for the standard Pico board's VBUS sense function.
- GPIO 26 is I2C1 SDA and ADC0.
- GPIO 27 is I2C1 SCL and ADC1.
- GPIO 28 is software PWM channel 23 and ADC2.
