# PicoPWM

[![PR Check](https://github.com/djwinter29-oss/PicoPWM/actions/workflows/pr-test.yml/badge.svg?branch=main)](https://github.com/djwinter29-oss/PicoPWM/actions/workflows/pr-test.yml)
[![Release](https://github.com/djwinter29-oss/PicoPWM/actions/workflows/release.yml/badge.svg)](https://github.com/djwinter29-oss/PicoPWM/actions/workflows/release.yml)
[![License: BSD 3-Clause](https://img.shields.io/badge/License-BSD%203--Clause-blue.svg)](LICENSE)
![Targets: RP2040 and RP2350](https://img.shields.io/badge/Targets-RP2040%20%7C%20RP2350-2674b8)

**24 PWM channels. One Pico. Three ways to control it.** PicoPWM turns a Raspberry Pi Pico or Pico 2 into a PWM
generator and frequency monitor with a USB CDC shell, an I2C register map and a USB HID interface. Each of three fixed
banks is a generator or a monitor, and the host sees the same logical channel IDs either way.

[Download firmware](https://github.com/djwinter29-oss/PicoPWM/releases) ·
[Pinout](docs/guides/pinout.md) ·
[Configuration](docs/guides/configuration.md) ·
[Control interfaces](docs/guides/control/README.md)

| Board | MCU | Firmware |
| --- | --- | --- |
| Raspberry Pi Pico | RP2040 | `pico` |
| Raspberry Pi Pico 2 | RP2350 | `pico2` |

## Available Today

| Capability | What you get today |
| --- | --- |
| **24 logical channels** | Three fixed 8-channel banks (A, B, C); each is a generator or a monitor, chosen in the startup configuration. |
| **Three backends** | Hardware PWM, PIO and software, each with its own frequency range, accuracy and resource limits. |
| **USB CDC shell** | Text commands at 115200 baud for channels, banks, clock, I2C address, version and board control. |
| **I2C slave** | Binary register map at 7-bit address `0x40` (default) for host-controller integration. |
| **USB HID and dashboards** | Browser (WebHID), Python and .NET dashboards read configuration and realized state and send commands. |
| **Persistent configuration** | Bank roles, I2C address and system clock are saved to flash and applied at the next reboot. |

The backends expose their tradeoffs instead of hiding them:

| Backend | Characteristics | Constraints |
| --- | --- | --- |
| Hardware PWM | High timing accuracy | Pin/slice assignment and minimum-frequency limits |
| PIO PWM | Flexible placement and broad range | Finite state machines and divider/period quantization |
| Software PWM | Any ordinary GPIO | CPU and timer overhead limit channel count, frequency and accuracy |
| Monitor backends | Measure input signals | Backend-specific range and accuracy; software monitoring suits simple low-frequency signals |

## Get Started

1. Get firmware for your board from [Releases](https://github.com/djwinter29-oss/PicoPWM/releases), or build it:
   `tools/firmware/build.sh --board pico` (or `pico2`). Output goes to `build/<board>/pico_pwm.uf2`.
2. Hold **BOOTSEL** while connecting the board over USB, copy the `.uf2` to the `RPI-RP2` drive, and let it reboot.
   `tools/firmware/load.sh --board pico` does this when the drive is mounted.
3. Connect to the USB CDC serial port at 115200 baud, or drive the I2C slave at `0x40` (see the
   [pinout](docs/guides/pinout.md) for wiring), or open a dashboard from [Dashboards](#dashboards).

GPIO signals are **3.3 V only**. I2C needs external pull-ups on SDA and SCL; start at 100 kHz.
Build prerequisites, version handling and test commands are in [BUILD_STRUCTURE.md](BUILD_STRUCTURE.md).

## Dashboards

| Dashboard | How to run it |
| --- | --- |
| **WebHID** | Static page for desktop Chrome or Edge, no backend; see [host/webhid](host/webhid/README.md). |
| **Python** | `tools/host/run-python.sh`, then open `http://127.0.0.1:5000/`; see [host/python](host/python/README.md). |
| **.NET** | `tools/host/run-dotnet.sh`; see [host/dotnet](host/dotnet/README.md). |

The Python and .NET hosts talk to the board over USB HID.

## Before You Connect

- After power-up the default configuration is monitor-only, so no PWM output is driven. Generator banks start at
  0 Hz and 0% duty; `stop` drives generator outputs low again and leaves monitor banks unchanged.
- Bank roles are chosen in the startup configuration, not at build time: update the target with `config set` (CDC),
  the matching I2C registers or HID, persist it with `config save`, then reboot.
- The default system clock is the SDK default (125 MHz RP2040, 150 MHz RP2350). 150, 200 and 250 MHz profiles are
  opt-in, and overclock profiles are not qualified for every board.
- The USB identity `0xCAFE:0x4012` is for development and lab use, not commercial derivatives. The CDC, HID and
  I2C interfaces are unauthenticated; see [SECURITY.md](SECURITY.md).
- Firmware has not been qualified on a hardware fixture; host tests do not replace hardware validation.

## Documentation

| Need | Guide |
| --- | --- |
| Wire and configure the board | [Pinout](docs/guides/pinout.md), [Firmware configuration](docs/guides/configuration.md) |
| Control it | [Control interfaces](docs/guides/control/README.md), [USB CDC CLI](docs/guides/control/usb_cdc_cli.md), [I2C protocol](docs/guides/control/i2c_protocol.md) |
| Understand the design | [Architecture](docs/design/architecture.md), [PWM driver design](docs/design/pwm_driver_design.md), [generators](docs/design/generator/) and [monitors](docs/design/monitor/) |
| Write a HID host | [CDC/HID overview](docs/design/usb/cdc-hid-overview.md), [HID report reference](docs/design/usb/hid-report-reference.md) |
| Build, test and release | [BUILD_STRUCTURE.md](BUILD_STRUCTURE.md), [documentation index](docs/README.md) |
| Contribute | [Contributing](CONTRIBUTING.md), [Code of Conduct](CODE_OF_CONDUCT.md), [Security](SECURITY.md) |

Related projects: [PicoUart](https://github.com/djwinter29-oss/PicoUart) (six USB serial ports) and
[PicoForge](https://github.com/djwinter29-oss/PicoForge) (host-controlled board services). They share no runtime
dependency. The CDC shell uses [embedded-cli](https://github.com/funbiscuit/embedded-cli), included as the
`firmware/third_party/embedded-cli` submodule (v0.1.4, MIT).

## License

[BSD 3-Clause](LICENSE).
