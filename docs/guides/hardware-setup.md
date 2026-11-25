# Hardware Setup

Wiring and GPIO assignments for Pico and Pico 2. The pin map is fixed in
[`board.c`](../../firmware/src/config/board.c); bank roles are selected at startup.

## Before Wiring

**GPIOs are 3.3 V only, not 5 V tolerant.** Connect board GND to the signal
source or instrument ground. Never connect generator outputs together.

The standard profiles use fixed channel-to-GPIO assignments. Custom board
profile constraints are described in the [PWM bank design](../design/pwm/banks.md).

## Board Compatibility

The `pico` and `pico2` profiles use the standard Pico and Pico 2 GPIO assignments.
Pico W and Pico 2 W can use the corresponding profiles; wireless functionality
is not enabled, and onboard LED control is unavailable on those boards.

For a third-party RP2040/RP2350 board, check GPIOs `0-22` and `28`, plus
`26` and `27` for I2C. The standard profile expects an LED on GPIO 25.
Check SWD access if using a Debug Probe.

## PWM Channels

Each bank has eight channels and one fixed backend. Each table cell gives
the logical channel and **GPIO number**, not a physical header-pin number.

| Local channel | Bank A: hardware PWM | Bank B: PIO | Bank C: software PWM |
| --- | --- | --- | --- |
| 0 | CH0 / GPIO 1 | CH8 / GPIO 0 | CH16 / GPIO 16 |
| 1 | CH1 / GPIO 3 | CH9 / GPIO 2 | CH17 / GPIO 17 |
| 2 | CH2 / GPIO 5 | CH10 / GPIO 4 | CH18 / GPIO 18 |
| 3 | CH3 / GPIO 7 | CH11 / GPIO 6 | CH19 / GPIO 19 |
| 4 | CH4 / GPIO 9 | CH12 / GPIO 8 | CH20 / GPIO 20 |
| 5 | CH5 / GPIO 11 | CH13 / GPIO 10 | CH21 / GPIO 21 |
| 6 | CH6 / GPIO 13 | CH14 / GPIO 12 | CH22 / GPIO 22 |
| 7 | CH7 / GPIO 15 | CH15 / GPIO 14 | CH23 / GPIO 28 |

Bank A uses PWM slices 0-7, output B. Bank B uses the companion slice-A
GPIOs for PIO. GPIO 28 also serves ADC2.

## I2C

The I2C control interface uses I2C1:

| Signal | GPIO |
|--------|------|
| SDA | GPIO 26 |
| SCL | GPIO 27 |

The default 7-bit address is `0x40`, configurable through USB. Fit external
pull-ups to 3.3 V, typically 4.7 kOhm; internal pull-ups are insufficient.
Use a 100 kHz or 400 kHz controller clock.
The [I2C reference](../protocols/i2c.md) defines transactions and registers.

## Shared Pins

- GPIO 25 is reserved for the on-board LED and is not assigned to a default PWM channel.
- GPIO 24 is reserved for the standard Pico board's VBUS sense function.
- GPIO 26 is I2C1 SDA and ADC0.
- GPIO 27 is I2C1 SCL and ADC1.
- GPIO 28 is software PWM channel 23 and ADC2.
