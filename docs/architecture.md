# Architecture

PicoPWM is built from a few layered modules. This page explains how they fit together.

## Module Overview

```
┌─────────────────────────────────────────────────────────────┐
│                         Applications                         │
│  (main.c demo + future command/script logic)                  │
└─────────────────────────────────────────────────────────────┘
                              │
┌─────────────────────────────────────────────────────────────┐
│                     Control Interface                        │
│  control.c / control.h                                       │
│  - 24 logical channels (0..7 HW, 8..23 SW)                   │
│  - per-channel: freq, duty, pulse_count                      │
└─────────────────────────────────────────────────────────────┘
        │                         │
┌───────────────┐         ┌─────────────────────┐
│  Hardware PWM  │         │   Software PWM       │
│  hw_pwm.c/h    │         │   sw_pwm.c/h         │
│  8 channels    │         │   16 channels        │
│  high frequency│         │   < 1 kHz            │
│  independent   │         │   timer interrupt    │
│  per-slice     │         │   driven             │
└───────────────┘         └─────────────────────┘
        │                         │
┌─────────────────────────────────────────────────────────────┐
│                     GPIO / Hardware                            │
│  RP2040 PWM slices, hardware timer, GPIO pins                │
└─────────────────────────────────────────────────────────────┘

┌─────────────────────────────────────────────────────────────┐
│                     Command Transports                         │
│  USB CDC (cdc_cmd.c/h)        I2C slave (i2c_slave.c/h)       │
│  text protocol                  binary protocol                 │
└─────────────────────────────────────────────────────────────┘
                              │
                   ┌─────────────────────┐
                   │   Shared Parser      │
                   │   cmd_parser.c/h     │
                   └─────────────────────┘
```

## Hardware PWM (8 channels)

RP2040 has 8 PWM slices, each with two output channels (A/B). The firmware uses **one channel per slice**, so each of the 8 hardware channels has a **fully independent frequency**.

- GPIO: 0, 2, 4, 6, 8, 10, 12, 14 (channel A of slices 0-7)
- Frequency range: approximately 7.5 Hz to 488 kHz at 125 MHz, or 9 Hz to 586 kHz at 150 MHz (8-bit resolution, TOP=255)
- Pulse counting: every PWM wrap triggers an interrupt that increments the channel's 32-bit pulse counter

The frequency is configured by selecting a clock divider (`clkdiv`) and a wrap value (`TOP`). The firmware prefers a fixed TOP=255 for a wide frequency range and predictable duty-cycle resolution.

```
f_pwm = f_sys / (clkdiv * (TOP + 1))
```

## Software PWM (16 channels)

The software PWM channels are driven by a periodic hardware timer interrupt. This allows each channel to have an arbitrary frequency below 1 kHz while maintaining accurate frequency timing.

- GPIO: 1, 3, 5, 7, 9, 11, 13, 15, 18, 19, 20, 21, 22, 25, 26, 27
- Frequency range: 0.1 Hz to 1 kHz
- Timer tick: 10 µs (100 kHz interrupt rate)
- Resolution at 1 kHz: 100 levels (1% duty steps)

The timer ISR maintains a per-channel counter. When the counter reaches the period value, the GPIO is toggled and the counter resets; the pulse counter is incremented at the same time.

Because the timer tick is shared, every software PWM frequency is an integer multiple of the tick rate. The firmware converts the requested frequency into the nearest integer period in ticks.

## Control Interface

`control.c` presents a single, uniform view of all 24 channels regardless of whether they are backed by hardware or software PWM.

Each channel has three properties:

| Property | Type | Description |
|----------|------|-------------|
| `freq` | `float` | Frequency in Hz. `0` means the channel is off. |
| `duty` | `float` | Duty cycle from 0.0 to 1.0. |
| `pulse_count` | `uint32_t` | Number of PWM periods completed. Wraps to 0 on overflow. |

Initialization values:
- `freq = 0` (off)
- `duty = 0.5` (50%)
- `pulse_count = 0`

The control layer delegates hardware PWM channels to `hw_pwm` and software PWM channels to `sw_pwm`.

**Important:** `pulse_count` is read-only at the control and transport layers. It cannot be set or reset directly. The `stop` command resets every channel to the power-up default state, which also clears `pulse_count` to zero for all 24 channels.

## Command Transports

### USB CDC (text)

The Pico appears as a USB serial device (CDC). A text-based command parser reads lines and calls the control API. Responses are printed back to the same interface.

### I2C Slave (binary)

GPIO 16/17 are configured as I2C0 slave at 7-bit address `0x40`. An interrupt-driven handler responds to read-only status queries from an I2C master. The protocol is byte-oriented and binary (little-endian).

Both transports expose the same data: device type, version, channel count, and per-channel properties.

## Startup Flow

1. `set_sys_clock_khz(150000, true)` — overclock to 150 MHz if possible
2. `stdio_init_all()` — initialize USB CDC
3. `hw_pwm_init()` — initialize PWM slices
4. `sw_pwm_init()` — initialize software PWM GPIO and timer interrupt
5. `control_init()` — set all 24 channels to default state
6. `cdc_cmd_init()` / `i2c_slave_init()` — start command interfaces
7. Configure demo pattern and print help
8. Main loop polls CDC and I2C handlers

## Performance Notes

- Hardware PWM wrap interrupts run at the PWM frequency. If all 8 hardware channels are running at high frequencies (e.g., > 100 kHz), total interrupt load can become significant. Disable counting or lower frequencies if you see instability.
- Software PWM runs at 100 kHz interrupt rate. With 16 channels, this is well within RP2040 capabilities at 150 MHz.
- USB CDC and I2C handlers are polled in the main loop. The 100 µs sleep keeps USB responsive without wasting CPU.
