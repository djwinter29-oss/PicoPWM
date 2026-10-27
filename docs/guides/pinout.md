# Pinout

This page defines the fixed physical mapping for every firmware image. The
logical channel IDs permanently identify one of three 8-channel banks.

Hardware PWM and PIO assignments have peripheral-specific pin and resource
constraints. Hardware PWM is fixed to one set of 8 slice-B GPIOs and PIO PWM is
fixed to the 8 companion slice-A GPIOs on the same PWM slices; neither backend
supports picking an arbitrary GPIO. Software PWM has no fixed peripheral pin
bank and may use any remaining GPIO not claimed by hardware PWM or PIO, still
subject to valid unique GPIO ownership and board functions such as the LED and
ADC pins.

On the standard Pico board, GPIO24 is the VBUS sense/input-related board pin
and GPIO25 is the onboard LED. Neither is assigned to the default software
channel map. A custom board profile may reclaim those numbers only when its
board wiring and policy explicitly support that choice.

GPIO23 and GPIO24 are optional software-only profile pins. They are not part
of the normal Pico channel map and cannot be assigned to hardware PWM or PIO
profiles.

## Fixed PWM Banks

| Bank | Logical channels | GPIOs | Allowed backend families |
|---|---:|---|---|
| Bank A | 0..7 | GPIO `1,3,5,7,9,11,13,15` | HW generator/monitor or SW generator/monitor |
| Bank B | 8..15 | GPIO `0,2,4,6,8,10,12,14` | PIO generator/monitor or SW generator/monitor |
| Bank C | 16..23 | GPIO `16,17,18,19,20,21,22,28` | SW generator or SW monitor only |

The startup `pwm_driver_config_t` selects one backend family and one role for
each bank. The mapping cannot be changed at runtime.

## PWM Channels

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

Bank A uses PWM slice channel-B pins intentionally. Bank B uses the companion
slice-A pins for PIO-capable routing. Bank C is software-only.

## I2C

The I2C control interface uses I2C1:

| Signal | GPIO |
|--------|------|
| SDA | GPIO 26 |
| SCL | GPIO 27 |

The 7-bit I2C address is `0x40`. External pull-up resistors are recommended;
typically use 4.7 kOhm pull-ups on SDA and SCL.

## Shared Pins

- GPIO 25 is reserved for the on-board LED and is not assigned to a default PWM channel.
- GPIO 24 is reserved for the standard Pico board's VBUS sense function.
- GPIO 26 is I2C1 SDA and ADC0.
- GPIO 27 is I2C1 SCL and ADC1.
- GPIO 28 is software PWM channel 23 and ADC2.
