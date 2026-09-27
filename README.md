# PicoPWM

PicoPWM is a Raspberry Pi Pico / Pico 2 firmware framework for controlling 24
logical PWM channels through two stable host interfaces: a USB CDC shell and an
I2C register protocol.

The project is intended for both:

- **Raspberry Pi Pico** based on **RP2040**
- **Raspberry Pi Pico 2** based on **RP2350**

Startup configuration selects whether each fixed Bank A/B/C is a generator or
monitor and which backend family it uses. The host sees the same logical
channel IDs and control commands for every valid configuration.

Each fixed channel allocation defines its GPIO, direction, capabilities,
frequency limits, and backend-local resource assignment.

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

Run the firmware host-side C tests independently of the ARM firmware build:

```sh
tools/test/test-firmware-c.sh
tools/test/coverage-firmware-c.sh
```

The root CMake project intentionally configures the firmware build only;
`firmware/tests` uses the host compiler and is managed by these test helpers.

Channel roles are chosen in the startup configuration, not at build time. The
host updates a target with `config set` over CDC or the matching I2C registers,
persists it with `config save`, and applies it by rebooting. See
[Firmware Configuration](docs/configuration.md#startup-bank-configuration) for
the full model.

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

### Host Management Interface

The optional Python host tool provides a Flask dashboard for reading and
controlling all 24 logical channels through either USB CDC or I2C. See
[host/python/README.md](host/python/README.md) for installation and connection
examples.

The matching C# ASP.NET Core host is available under
[host/dotnet](host/dotnet/README.md) for .NET 8 environments.

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

Use the `stop` command to apply the configured banks' safe reset behavior.
Generator banks stop outputs; monitor banks leave measured input channels
unchanged. `pulse_count` is monotonic from power-on and is not reset by `stop`.

---

## Documentation

- [Architecture](docs/architecture.md)
- [Firmware Configuration](docs/configuration.md)
- [PWM Driver Configuration](docs/pwm_driver_config.md)
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
- `host/python/` — Python client, CDC/I2C backends, and Flask management UI
- `host/dotnet/` — C# ASP.NET Core client, CDC/I2C backends, and web UI
- `docs/` — user and design documentation
- `tools/firmware/` — Linux firmware build and flashing helpers
- `tools/test/` — Linux CTest and coverage helpers
- `README.md` — top-level project overview

---

## Default Generator State

After power-up or reset, the default configuration is monitor-only, so no PWM
outputs are actively driven. If generator banks are configured, they start
with their outputs off:

| Property | Value |
|----------|-------|
| Frequency | 0 Hz (off) |
| Duty | 0% |
| Pulse count | 0 |

No demo channels are configured. Use the USB CDC shell or I2C commands to set
frequencies and duty cycles. Monitor banks report input state instead of using
this output-default state.

Use the `stop` command to reset all channels back to this state at any time. `pulse_count` continues accumulating from power-on.

---

## License

This firmware is provided as-is for embedded development and experimentation.
