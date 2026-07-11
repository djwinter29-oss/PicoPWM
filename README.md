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

- Install the [Raspberry Pi Pico SDK](https://github.com/raspberrypi/pico-sdk).
- Put `cmake`, a supported build tool, and `arm-none-eabi-gcc` on your `PATH`.
- Set `PICO_SDK_PATH`.

### Build

Build the firmware from the repository `firmware/` directory:

```bash
cd firmware
mkdir build && cd build
cmake -DPICO_SDK_PATH=%PICO_SDK_PATH% -G "MinGW Makefiles" ..
make -j4
```

On Linux or macOS, use the same flow but point CMake at `$PICO_SDK_PATH`.

If `make` is not the right backend on your machine, use a generator that matches your toolchain. For example, on Windows with Ninja:

```bash
cd firmware
mkdir build && cd build
cmake -DPICO_SDK_PATH=%PICO_SDK_PATH% -G Ninja ..
ninja
```

Build outputs of interest:

- `pico_pwm.uf2` for USB flashing
- `pico_pwm.elf` for debug tools

Helper scripts are provided under:

- `tools/windows/` for PowerShell build, test, and load helpers
- `tools/linux/` for POSIX shell build, test, and load helpers

Examples:

```powershell
tools\windows\build.ps1
tools\windows\test.ps1
tools\windows\load.ps1
tools\windows\coverage.ps1
```

```sh
./tools/linux/build.sh
./tools/linux/test.sh
./tools/linux/load.sh
./tools/linux/coverage.sh
```

Coverage helpers expect:

- `gcovr` on `PATH`
- CMake tests configured in the active build tree

The current repository does not define any CMake tests yet, so the coverage scripts stop with a clear error until tests are added.

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
- I2C slave at address `0x40` on GPIO 16/17

### Troubleshooting

- Confirm `PICO_SDK_PATH` is visible to the shell running CMake.
- Confirm `arm-none-eabi-gcc` is on your `PATH`.
- If the Pico SDK checkout is incomplete, run `git submodule update --init --recursive` inside the SDK.
- If USB CDC does not enumerate, reconnect the cable and confirm it supports data.
- If I2C does not respond, confirm 4.7 kΩ pull-ups on GPIO 16/17 and start at 100 kHz.

If you are documenting or preparing the monitoring build, keep the same physical channel order and pinout shown below so generator and monitoring firmware stay interchangeable at the harness level.

---

## Channel Map

| Logical Channel | Type | GPIO | Notes |
|-----------------|------|------|-------|
| 0 | Hardware PWM | GPIO 1 | Slice 0, channel B |
| 1 | Hardware PWM | GPIO 3 | Slice 1, channel B |
| 2 | Hardware PWM | GPIO 5 | Slice 2, channel B |
| 3 | Hardware PWM | GPIO 7 | Slice 3, channel B |
| 4 | Hardware PWM | GPIO 9 | Slice 4, channel B |
| 5 | Hardware PWM | GPIO 11 | Slice 5, channel B |
| 6 | Hardware PWM | GPIO 13 | Slice 6, channel B |
| 7 | Hardware PWM | GPIO 15 | Slice 7, channel B |
| 8 | PIO PWM | GPIO 0 | Companion pin to HW channel 0 |
| 9 | PIO PWM | GPIO 2 | Companion pin to HW channel 1 |
| 10 | PIO PWM | GPIO 4 | Companion pin to HW channel 2 |
| 11 | PIO PWM | GPIO 6 | Companion pin to HW channel 3 |
| 12 | PIO PWM | GPIO 8 | Companion pin to HW channel 4 |
| 13 | PIO PWM | GPIO 10 | Companion pin to HW channel 5 |
| 14 | PIO PWM | GPIO 12 | Companion pin to HW channel 6 |
| 15 | PIO PWM | GPIO 14 | Companion pin to HW channel 7 |
| 16 | Software PWM | GPIO 18 | |
| 17 | Software PWM | GPIO 19 | |
| 18 | Software PWM | GPIO 20 | |
| 19 | Software PWM | GPIO 21 | |
| 20 | Software PWM | GPIO 22 | |
| 21 | Software PWM | GPIO 25 | On-board LED, optional |
| 22 | Software PWM | GPIO 26 | Shared with ADC0 |
| 23 | Software PWM | GPIO 27 | Shared with ADC1 |

### Target Frequency Ranges

| Backend | Target Range | Positioning |
|---------|--------------|-------------|
| Hardware PWM | about **10 Hz to 1 MHz** | Best accuracy and best fit for measurement-compatible channels |
| PIO PWM | about **1 Hz to 1 MHz** | Flexible timing over the intended generator range; realized frequency is quantized by PIO divider and period search |
| Software PWM | about **1 Hz to 1 kHz** | Lowest cost backend for slower signals |

---

## Command Interfaces

- **USB CDC serial**: text commands at 115200 baud
- **I2C slave**: binary register map at 7-bit address `0x40` on GPIO 16 (SDA) / GPIO 17 (SCL)

Use the `stop` command to reset all channels to the power-up state: frequency = 0 Hz and duty = 50%. `pulse_count` is monotonic from power-on and is not reset by `stop`.

---

## Documentation

- [Architecture](docs/architecture.md)
- [Control Protocol](docs/protocol.md)
- [Firmware Interfaces](docs/firmware_interfaces.md)

## Repository Layout

- `firmware/` — CMake project, Pico SDK import, and all firmware source code
- `docs/` — user and design documentation
- `README.md` — top-level project overview

---

## Default State

After power-up or reset, **all 24 channels are off**:

| Property | Value |
|----------|-------|
| Frequency | 0 Hz (off) |
| Duty | 50% |
| Pulse count | 0 |

No demo channels are configured. Use CDC or I2C commands to set frequencies and duty cycles.

Use the `stop` command to reset all channels back to this state at any time. `pulse_count` continues accumulating from power-on.

---

## License

This firmware is provided as-is for embedded development and experimentation.
