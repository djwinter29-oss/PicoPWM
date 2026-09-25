# PicoPWM

PicoPWM is a Raspberry Pi Pico / Pico 2 project aimed at providing a low-cost PWM platform with two host control interfaces: USB CDC CLI and I2C.

The project is intended for both:

- **Raspberry Pi Pico** based on **RP2040**
- **Raspberry Pi Pico 2** based on **RP2350**

The project targets two firmware variants:

- **PWM generator** — drives 24 logical PWM outputs.
- **PWM monitoring** — keeps the same external pin layout so the same board wiring can be reused for measurement-focused firmware.

The generator-oriented channel plan is:

- **8 hardware PWM channels** on the MCU PWM slice **channel B** pins for the highest accuracy and for pin compatibility with monitoring use cases.
- **8 PIO PWM channels** for flexible timing across the intended **1 Hz to 1 MHz** operating range.
- **8 software PWM channels** focused on about **1 Hz to 1 kHz** operation.

Each logical channel exposes frequency, duty cycle, and a read-only 32-bit pulse counter through a unified control model.

The current firmware exposes:

- a USB CDC CLI for interactive control
- an I2C register map for host-controller integration
- board-level LED and reboot control through both interfaces

---

## Quick Start

### Prerequisites

- Install `git` and the [Raspberry Pi Pico SDK prerequisites](https://datasheets.raspberrypi.com/pico/getting-started-with-pico.pdf).
- Put `cmake`, a supported build tool, and `arm-none-eabi-gcc` on your `PATH`.
- The setup helper downloads the pinned Pico SDK into `.pico-sdk`.

### Build

The Linux helper scripts select a suitable CMake generator and keep Pico and
Pico 2 build directories separate. From the repository root:

On Linux or macOS:

```sh
. tools/firmware/setup-sdk-env.sh
./tools/firmware/build.sh --board pico
```

Use `pico2` instead of `pico` for Raspberry Pi Pico 2. The matching helpers
under `tools/firmware/` handle loading, while `tools/test/` contains the CTest
and syntax-check entry points. A direct CMake build is also possible from
`firmware/` when a custom generator or build layout is needed.

Build outputs of interest:

- `pico_pwm.uf2` for USB flashing
- `pico_pwm.elf` for debug tools

Firmware versioning:

- local builds default to firmware version `0.0.0-dev`
- pass `-DPICO_PWM_FIRMWARE_VERSION=x.y.z` to CMake, or set `PICO_PWM_FIRMWARE_VERSION`, to override it
- the `version` CDC command and the I2C `REG_VERSION` register both return this build-time firmware version
- the release workflow triggers on tags matching `vx.y.z` and builds the firmware with version `x.y.z`
- a normal local CMake build does not require Git tags or GitHub Actions; it builds with the default version unless you override it

### Flash

1. Hold **BOOTSEL** while connecting the board over USB.
2. Copy `pico_pwm.uf2` to the `RPI-RP2` drive.
3. Let the board reboot normally.

Other flash options:

- `picotool load pico_pwm.uf2`
- `picotool reboot`
- SWD / OpenOCD with `pico_pwm.elf` if you are using a debug probe

### Connect

- USB CDC serial at **115200 baud**
- I2C slave at address `0x40`; see [Pinout](docs/pinout.md) for physical connections.

### Troubleshooting

- Confirm `PICO_SDK_PATH` is visible to the shell running CMake.
- Confirm `arm-none-eabi-gcc` is on your `PATH`.
- If the Pico SDK checkout is incomplete, run `git submodule update --init --recursive` inside the SDK.
- If USB CDC does not enumerate, reconnect the cable and confirm it supports data.
- If I2C does not respond, confirm external pull-ups on SDA/SCL and start at 100 kHz; see [Pinout](docs/pinout.md).

---

## Pinout

See [docs/pinout.md](docs/pinout.md) for the complete PWM channel and host-interface pinout.

### Target Frequency Ranges

| Backend | Target Range | Positioning |
|---------|--------------|-------------|
| Hardware PWM | about **10 Hz to 1 MHz** | Best accuracy and best fit for measurement-compatible channels |
| PIO PWM | about **1 Hz to 1 MHz** | Flexible timing over the intended generator range; realized frequency is quantized by PIO divider and period search |
| Software PWM | about **1 Hz to 1 kHz** | Lowest cost backend for slower signals |

---

## Command Interfaces

- **USB CDC serial**: text commands at 115200 baud
- **I2C slave**: binary register map at 7-bit address `0x40`; see [Pinout](docs/pinout.md) for physical connections

Use the `stop` command to reset all channels to the power-up state: frequency = 0 Hz and duty = 0%. `pulse_count` is monotonic from power-on and is not reset by `stop`.

---

## Documentation

- [Architecture](docs/architecture.md)
- [Firmware Configuration](docs/configuration.md)
- [Control Protocol](docs/protocol.md)
- [USB CDC CLI](docs/usb_cdc_cli.md)
- [Firmware Interfaces](docs/firmware_interfaces.md)
- [Pinout](docs/pinout.md)

The USB CDC CLI uses the vendored [microrl](https://github.com/Helius/microrl)
line editor, pinned to commit `d044bf4`. Its Apache-2.0 license and source are
included under `firmware/third_party/microrl/`.

## Related Project

[PicoUART](https://github.com/djwinter29-oss/PicoUART) is the companion
Raspberry Pi Pico project for UART-oriented host communication. PicoPWM is a
separate firmware and keeps its host interfaces focused on USB CDC and I2C;
the projects can be used as related building blocks without sharing a runtime
dependency.

## Repository Layout

- `firmware/` — CMake project, Pico SDK import, and all firmware source code
- `docs/` — user and design documentation
- `tools/firmware/` — Linux firmware build and flashing helpers
- `tools/test/` — Linux CTest and coverage helpers
- `README.md` — top-level project overview

---

## Default State

After power-up or reset, **all 24 channels are off**:

| Property | Value |
|----------|-------|
| Frequency | 0 Hz (off) |
| Duty | 0% |
| Pulse count | 0 |

No demo channels are configured. Use CDC or I2C commands to set frequencies and duty cycles.

Use the `stop` command to reset all channels back to this state at any time. `pulse_count` continues accumulating from power-on.

---

## License

This firmware is provided as-is for embedded development and experimentation.
