# PicoPWM

PicoPWM is a Raspberry Pi Pico / Pico 2 firmware framework for controlling 24
logical PWM channels through two stable host interfaces: a USB CDC shell and an
I2C register protocol.

The project is intended for both:

- **Raspberry Pi Pico** based on **RP2040**
- **Raspberry Pi Pico 2** based on **RP2350**

Build profiles select whether each logical channel is a generator or monitor,
and whether it uses the hardware PWM, PIO, or software backend. The host sees
the same logical channel IDs and control commands regardless of profile.

Each channel profile also defines its GPIO, direction, capabilities, frequency
limits, and backend-local resource assignment.

The framework intentionally exposes backend tradeoffs rather than hiding them:

- **Hardware PWM** provides high timing accuracy but has pin/slice constraints
	and a backend-specific minimum frequency.
- **PIO PWM** provides flexible pin placement and a broad range, with finite
	PIO state-machine resources and divider/period quantization.
- **Software PWM** has no fixed PWM slice or PIO channel allocation and can use
	ordinary GPIOs, but polling/timer and CPU overhead limit channel count,
	maximum frequency, and timing accuracy.
- **Monitor backends** have their own measurable frequency range and accuracy;
	input channels are read-only through the control interfaces. Software
	monitoring is intended for simple low-frequency measurements; high-
	performance monitoring should use PIO or hardware capture.

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

Select the channel profile when configuring the firmware. The profile is a
build-time decision: it selects the compiled backend sources, PIO program,
monitor/generator resources, and channel table. It is not changed at runtime.

```sh
cmake -S firmware -B build-generator \
	-DPICO_PWM_PROFILE=generator

cmake -S firmware -B build-monitor \
	-DPICO_PWM_PROFILE=monitor

cmake -S firmware -B build-mixed \
	-DPICO_PWM_PROFILE=mixed
```

The current profiles use the default 24-channel pin arrangement. Project-specific
profiles can later change backend, direction, GPIO, and capabilities while
preserving the host-facing control model.

The all-software profiles use 24 logical software channels on GPIO `0..15`,
`18..22`, and `26..28`; GPIO `16` and `17` remain reserved for I2C and GPIO
`25` remains available for the board LED. This is a different
physical arrangement from the mixed hardware/PIO/software default profile.

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

### Backend Characteristics

| Backend | Characteristics | Constraints |
|---------|--------------|-------------|
| Hardware PWM | High timing accuracy | Pin/slice assignment and minimum-frequency limits |
| PIO PWM | Flexible placement and broad range | Finite state machines and divider/period quantization |
| Software PWM | Simple polling/timer implementation | Lower maximum frequency and timing accuracy |
| Monitor backends | Measure input signals | Backend-specific measurable range and accuracy |

---

## Command Interfaces

- **USB CDC serial**: text commands at 115200 baud
- **I2C slave**: binary register map at 7-bit address `0x40`; see [Pinout](docs/pinout.md) for physical connections

Use the `stop` command to apply the selected profile's safe reset behavior.
Generator profiles stop outputs; monitor profiles leave measured input channels
unchanged. `pulse_count` is monotonic from power-on and is not reset by `stop`.

---

## Documentation

- [Architecture](docs/architecture.md)
- [Firmware Configuration](docs/configuration.md)
- [Profile Authoring](docs/profile_authoring.md)
- [Control Interfaces](docs/control/README.md)
- [I2C Protocol](docs/control/i2c_protocol.md)
- [USB CDC CLI](docs/control/usb_cdc_cli.md)
- [Pinout](docs/pinout.md)

The USB CDC shell uses the vendored [microrl](https://github.com/Helius/microrl)
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

## Default Generator State

In a generator profile, after power-up or reset, **all 24 output channels are
off**:

| Property | Value |
|----------|-------|
| Frequency | 0 Hz (off) |
| Duty | 0% |
| Pulse count | 0 |

No demo channels are configured. Use the USB CDC shell or I2C commands to set
frequencies and duty cycles. Monitor profiles report input state instead of
using this output-default state.

Use the `stop` command to reset all channels back to this state at any time. `pulse_count` continues accumulating from power-on.

---

## License

This firmware is provided as-is for embedded development and experimentation.
