# Pinout

This page is the canonical physical pin and logical-channel mapping for
PicoPWM. The same channel order is intended for generator and monitoring
firmware so a harness can be reused between firmware variants.

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
| 16 | Software PWM | 0 | GPIO 18 | |
| 17 | Software PWM | 1 | GPIO 19 | |
| 18 | Software PWM | 2 | GPIO 20 | |
| 19 | Software PWM | 3 | GPIO 21 | |
| 20 | Software PWM | 4 | GPIO 22 | |
| 21 | Software PWM | 5 | GPIO 25 | On-board LED, optional |
| 22 | Software PWM | 6 | GPIO 26 | Shared with ADC0 |
| 23 | Software PWM | 7 | GPIO 27 | Shared with ADC1 |

The hardware bank uses PWM slice channel B pins intentionally. This keeps the
external channel order aligned with monitoring-oriented firmware.

## I2C

The I2C control interface uses I2C0:

| Signal | GPIO |
|--------|------|
| SDA | GPIO 16 |
| SCL | GPIO 17 |

The 7-bit I2C address is `0x40`. External pull-up resistors are recommended;
typically use 4.7 kOhm pull-ups on SDA and SCL.

## Shared Pins

- GPIO 25 is the optional on-board LED output and software PWM channel 21.
- GPIO 26 is software PWM channel 22 and ADC0.
- GPIO 27 is software PWM channel 23 and ADC1.
