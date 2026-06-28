# PicoPWM

A Raspberry Pi Pico (RP2040) firmware that turns the board into a multi-channel PWM generator with a unified control interface.

- **24 logical PWM channels** — 8 hardware PWM channels (high frequency) + 16 software PWM channels (< 1 kHz)
- **Per-channel properties**: frequency, duty cycle, and **read-only** 32-bit pulse counter
- **Dual-core**: Core 0 handles USB CDC + I2C, Core 1 manages all PWM hardware
- **System clock**: 150 MHz for extra headroom

---

## Quick Start

1. Install the [Raspberry Pi Pico SDK](https://github.com/raspberrypi/pico-sdk).
2. Set `PICO_SDK_PATH` environment variable.
3. Build and flash:

```bash
cd C:\Users\ji\Documents\PicoPWM
mkdir build && cd build
cmake -DPICO_SDK_PATH=%PICO_SDK_PATH% -G "MinGW Makefiles" ..
make -j4
```

Copy `pico_pwm.uf2` to the Pico in BOOTSEL mode.

4. Connect via USB serial at **115200 baud** or via **I2C0** on GPIO 16/17 (address `0x40`).

---

## Channel Map

| Logical Channel | Type | GPIO | Notes |
|-----------------|------|------|-------|
| 0 | Hardware PWM | GPIO 0 | Slice 0, channel A |
| 1 | Hardware PWM | GPIO 2 | Slice 1, channel A |
| 2 | Hardware PWM | GPIO 4 | Slice 2, channel A |
| 3 | Hardware PWM | GPIO 6 | Slice 3, channel A |
| 4 | Hardware PWM | GPIO 8 | Slice 4, channel A |
| 5 | Hardware PWM | GPIO 10 | Slice 5, channel A |
| 6 | Hardware PWM | GPIO 12 | Slice 6, channel A |
| 7 | Hardware PWM | GPIO 14 | Slice 7, channel A |
| 8 | Software PWM | GPIO 1 | |
| 9 | Software PWM | GPIO 3 | |
| 10 | Software PWM | GPIO 5 | |
| 11 | Software PWM | GPIO 7 | |
| 12 | Software PWM | GPIO 9 | |
| 13 | Software PWM | GPIO 11 | |
| 14 | Software PWM | GPIO 13 | |
| 15 | Software PWM | GPIO 15 | |
| 16 | Software PWM | GPIO 18 | |
| 17 | Software PWM | GPIO 19 | |
| 18 | Software PWM | GPIO 20 | |
| 19 | Software PWM | GPIO 21 | |
| 20 | Software PWM | GPIO 22 | |
| 21 | Software PWM | GPIO 25 | On-board LED, optional |
| 22 | Software PWM | GPIO 26 | Shared with ADC0 |
| 23 | Software PWM | GPIO 27 | Shared with ADC1 |

---

## Command Interfaces

- **USB CDC serial**: text commands at 115200 baud (see `docs/protocol.md`)
- **I2C slave**: 7-bit address `0x40` on GPIO 16 (SDA) / GPIO 17 (SCL)

Use the `stop` command to reset all channels to the power-up state: frequency = 0 Hz, duty = 50%, pulse_count = 0.

---

## Documentation

- [Architecture](docs/architecture.md)
- [Control Protocol](docs/protocol.md)
- [Hardware Notes](docs/hardware.md)
- [Internal API](docs/api.md)
- [Build & Flash](docs/build.md)

---

## Default State

After power-up or reset, **all 24 channels are off**:

| Property | Value |
|----------|-------|
| Frequency | 0 Hz (off) |
| Duty | 50% |
| Pulse count | 0 |

No demo channels are configured. Use CDC or I2C commands to set frequencies and duty cycles.

Use the `stop` command to reset all channels back to this state at any time.

---

## License

This firmware is provided as-is for embedded development and experimentation.
