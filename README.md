# PicoPWM

[![PR Check](https://github.com/djwinter29-oss/PicoPWM/actions/workflows/pr-test.yml/badge.svg?branch=main)](https://github.com/djwinter29-oss/PicoPWM/actions/workflows/pr-test.yml)
[![Release](https://github.com/djwinter29-oss/PicoPWM/actions/workflows/release.yml/badge.svg)](https://github.com/djwinter29-oss/PicoPWM/actions/workflows/release.yml)
[![License: BSD 3-Clause](https://img.shields.io/badge/License-BSD%203--Clause-blue.svg)](LICENSE)
![Targets: RP2040 and RP2350](https://img.shields.io/badge/Targets-RP2040%20%7C%20RP2350-2674b8)

**24 PWM channels. One Pico. Three ways to control it.** PicoPWM turns a Raspberry Pi Pico or Pico 2 into a PWM
generator and frequency monitor, controlled through USB CDC, I2C or USB HID. The firmware exposes three fixed
8-channel banks. Configure each bank as a generator or monitor; channel IDs stay the same across control interfaces.

**Quick links:** [Download firmware](https://github.com/djwinter29-oss/PicoPWM/releases) ·
[Hardware setup](docs/guides/hardware-setup.md) ·
[Open dashboard](https://djwinter29-oss.github.io/PicoPWM/) ·
[Python package](https://pypi.org/project/picopwm/)

## Why PicoPWM?

| | What you get |
| --- | --- |
| **Choose the right PWM engine** | Hardware PWM, PIO and software backends, each with distinct frequency, accuracy and resource tradeoffs. |
| **PWM control over I2C** | Read realized channel state, set generator outputs, and stop all generator outputs from an external controller. |
| **Live state, not just commands** | Read the realized channel frequency and duty as well as device configuration and status. |
| **Configuration survives reboot** | Save bank roles, I2C address and system clock to flash; changes take effect after reboot. |

![WebHID dashboard with simulated device identity, boot configuration, runtime status, and 24 PWM generator and monitor channels.](docs/assets/pico-pwm-dashboard.png)

_Illustrative WebHID dashboard with simulated readings, not a live hardware result. The Python and .NET dashboards share
this layout; labels and columns may differ slightly in the current release._

| Backend | Characteristics | Constraints |
| --- | --- | --- |
| Hardware PWM | Hardware-timed PWM | Generator requests: 10 Hz-1 MHz; fixed pin/slice assignment |
| PIO PWM | Fixed Bank B pin map; broad range | Generator requests: 1 Hz-1 MHz; finite state machines and divider/period quantization |
| Software PWM | Fixed Bank C pin map | Generator requests: 1 Hz-1 kHz; CPU and timer overhead limit channel count and accuracy |
| Monitor backends | Measure input signals | Backend-specific range and accuracy; software monitoring suits simple low-frequency signals |

These are generator request ranges; `0 Hz` is a special static output level (`0%` low or `100%` high). Duty is
0-100%. Monitor measurement limits are backend-specific.

## Before You Connect

- GPIO signals are **3.3 V only**; do not connect 5 V signals.
- I2C needs external pull-ups on SDA and SCL. Start at 100 kHz.
- The default configuration is monitor-only, so no PWM output is driven after power-up. Generator banks start at
   0 Hz and 0% duty.
- Bank roles are set in the startup configuration, not at build time. Change them over CDC (`config set`) or HID.
  The documented I2C contract is for PWM channel control and does not include bank-role or device configuration.
- Start with the default system clock. Higher clock profiles may overclock the board and are not qualified for
  every board; see the [configuration guide](docs/guides/setup-and-troubleshooting.md#configure-bank-roles).
- Firmware has not been qualified on a hardware fixture; host tests do not replace hardware validation.

## Get Started

This walkthrough uses the USB CDC shell. **Supported boards: Raspberry Pi Pico and Pico 2; Pico W and Pico 2 W
are not supported.** For controller-based integration instead, see the
[I2C protocol](docs/design/i2c/i2c-protocol.md) (default 7-bit address `0x40`).

1. **Get firmware.** Download the `pico` UF2 for Raspberry Pi Pico (RP2040) or `pico2` for Raspberry Pi Pico 2
   (RP2350) from [Releases](https://github.com/djwinter29-oss/PicoPWM/releases), or build one with
   `tools/firmware/build.sh --board pico` (or `pico2`). The UF2 is written to `build/<board>/pico_pwm.uf2`.
2. **Flash the UF2.** Hold **BOOTSEL** while connecting the board, release it when the boot drive appears, then copy
   the downloaded or locally built `.uf2` onto that drive. The board reboots automatically.
3. **Wire your signals.** Follow the [hardware setup guide](docs/guides/hardware-setup.md) for the channel GPIO map,
   I2C pins and board-specific pin considerations.
4. **Open the CDC shell.** Open the board's USB CDC serial port in a terminal. Serial device names vary by OS;
   common examples are `/dev/ttyACM0` on Linux, `COMx` on Windows, and `/dev/cu.usbmodem*` on macOS.
   USB CDC does not require a particular terminal baud-rate setting. See the
   [USB CDC shell guide](docs/guides/usb-cdc-shell.md) for terminal setup.
5. **Verify.** In the CDC shell, run `board get version` to confirm the firmware is running, then
   `config get` to see the bank roles. See the [USB CDC shell guide](docs/guides/usb-cdc-shell.md) for command details.
6. **Open a dashboard (optional).** Use the [browser dashboard](https://djwinter29-oss.github.io/PicoPWM/) or run one
   locally as described below.

The default bank roles are monitor-only. To try a generator output, connect a 3.3 V-safe oscilloscope or logic
analyzer to GPIO 1 and GND, then configure Bank A and reboot:

```text
config set banka=generator
```

The command saves the target setting and reboots the board. After it reconnects, run `pwm set 0 1000 50` and
`pwm get 0` to request 1 kHz at 50% duty and inspect the realized output. When finished, run `pwm stop all` to stop
generator outputs; the saved bank role remains unchanged. See the [CDC shell guide](docs/guides/usb-cdc-shell.md)
for command details.

For symptom-based checks, configuration help, and HID access troubleshooting,
see the [setup and troubleshooting guide](docs/guides/setup-and-troubleshooting.md).

## Dashboards

Use a dashboard for interactive control and monitoring. USB CDC and I2C remain available to scripts and other host
applications.

### Browser Dashboard

Open the [hosted dashboard](https://djwinter29-oss.github.io/PicoPWM/) in desktop Chrome or Edge, connect the board to
that computer, and select **Connect**. The browser talks to the board directly over WebHID; no local server is needed.
See the [WebHID guide](host/webhid/README.md) for browser requirements and controls.

### Local Python Dashboard

With Python 3.10+ in an activated virtual environment, install the [PyPI package](https://pypi.org/project/picopwm/)
and start the dashboard:

```sh
python -m pip install picopwm
picopwm-web
```

Open <http://127.0.0.1:5000/> and select your board. The Python server connects over USB HID.
See the [Python guide](host/python/README.md) for virtual environment setup, HID permissions, and client usage.

For development from a repository checkout, use `tools/host/run-python.sh` instead.

### Local .NET Dashboard

Download and extract the `picopwm-host-v<VERSION>-<RID>` archive for your platform from
[Releases](https://github.com/djwinter29-oss/PicoPWM/releases). Run `./PicoPwm` on Linux/macOS or
`.\PicoPwm.exe` in Windows PowerShell from the extracted directory, then open <http://127.0.0.1:5000/>.
The application includes its .NET runtime and connects to the board over USB HID.

For development from a repository checkout, use `tools/host/run-dotnet.sh` instead.
See the [.NET guide](host/dotnet/README.md) for configuration and API details, and
[Builds and publishing](docs/deployment/releases.md) for release platforms and package details.

For configuration help and connection or HID troubleshooting, see the
[setup and troubleshooting guide](docs/guides/setup-and-troubleshooting.md).

## Hardware Compatibility

The `pico` and `pico2` profiles use the standard Pico and Pico 2 GPIO assignments documented in the
[hardware setup guide](docs/guides/hardware-setup.md). A third-party RP2040/RP2350 board is not automatically
compatible: verify it exposes the profile's channel GPIOs `0-22` and `28`; I2C additionally requires GPIOs `26` and
`27`. The standard profile also assumes the board's LED is on GPIO 25. Verify SWD access only if programming it with a
Debug Probe.
`tools/firmware/load.sh` programs a local build through a Debug Probe; see
[Program with a Pico Debug Probe](docs/guides/hardware-setup.md#program-with-a-pico-debug-probe). Its
`--flash-size <bytes>` option overrides OpenOCD's flash-size assumption only; it does not change the firmware's GPIO
mapping or board profile.

**Pico W and Pico 2 W** are not supported: the build helper and releases cover only the `pico` and `pico2` boards.

## Documentation

| Need | Resource |
| --- | --- |
| Wire, configure, and troubleshoot | [Hardware setup](docs/guides/hardware-setup.md), [Setup and troubleshooting](docs/guides/setup-and-troubleshooting.md) |
| Control it | [USB CDC shell](docs/guides/usb-cdc-shell.md), [I2C protocol](docs/design/i2c/i2c-protocol.md), [USB interface overview](docs/design/usb/usb-interface-overview.md) |
| Understand the design | [Architecture](docs/design/architecture.md), [PWM driver design](docs/design/pwm-driver/driver-design.md), [PWM backend designs](docs/design/pwm-driver/) |
| Write a HID host | [USB interface overview](docs/design/usb/usb-interface-overview.md), [HID report reference](docs/design/usb/hid-report-reference.md) |
| Build, test and release | [Contributing](CONTRIBUTING.md), [Builds and publishing](docs/deployment/releases.md) |
| Contribute | [Contributing](CONTRIBUTING.md), [Code of Conduct](CODE_OF_CONDUCT.md), [Security](SECURITY.md) |

The USB identity `0xCAFE:0x4012` is for development and lab use, not commercial derivatives. The CDC, HID and I2C
interfaces are unauthenticated; see [SECURITY.md](SECURITY.md).

Related projects: [PicoUART](https://github.com/djwinter29-oss/PicoUart) (six USB serial ports) and
[PicoForge](https://github.com/djwinter29-oss/PicoForge) (host-controlled board services). They share no runtime
dependency.

Third-party software: the CDC shell uses [embedded-cli](https://github.com/funbiscuit/embedded-cli), included as the
`firmware/third_party/embedded-cli` submodule (v0.1.4, MIT).

## License

[BSD 3-Clause](LICENSE).
