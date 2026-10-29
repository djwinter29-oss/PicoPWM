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
[Open browser dashboard](https://djwinter29-oss.github.io/PicoPWM/) ·
[Python dashboard](host/python/README.md) ·
[USB CDC shell](docs/guides/usb_cdc_shell.md) ·
[I2C protocol](docs/guides/i2c_protocol.md)

## Why PicoPWM?

| | What you get |
| --- | --- |
| **24 channels, one stable map** | Three fixed 8-channel banks (A, B, C), configured independently as generators or monitors at startup. |
| **Choose the right PWM engine** | Hardware PWM, PIO and software backends, each with distinct frequency, accuracy and resource tradeoffs. |
| **Three control interfaces** | A USB CDC command shell, an I2C register map, and USB HID for dashboards and host applications. |
| **Live state, not just commands** | Read the realized channel frequency and duty as well as device configuration and status. |
| **Configuration survives reboot** | Save bank roles, I2C address and system clock to flash; changes take effect after reboot. |

![WebHID dashboard with simulated device identity, boot configuration, runtime status, and 24 PWM generator and monitor channels.](docs/assets/pico-pwm-dashboard.png)

_Illustrative WebHID dashboard with simulated readings, not a live hardware result. The Python and .NET dashboards share
this layout; labels and columns may differ slightly in the current release._

| Backend | Characteristics | Constraints |
| --- | --- | --- |
| Hardware PWM | High timing accuracy | Generator requests: 10 Hz-1 MHz; fixed pin/slice assignment |
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
- Bank roles are set in the startup configuration, not at build time. Change them over CDC (`config set`), I2C
  (`REG_CONFIG_SET`) or HID. CDC validates, saves and reboots for a `config set` request; I2C and HID use their own
  save and reboot operations (`REG_CONFIG_SAVE` or the dashboard's **Save & Reboot**).
- The SDK default is 125 MHz on RP2040 and 150 MHz on RP2350. The 150 MHz profile is an overclock only on RP2040.
   HID/dashboard saves require acknowledgement above 150 MHz; CDC has no separate acknowledgement step. The 200 and
   250 MHz profiles are not qualified for every board.
- Firmware has not been qualified on a hardware fixture; host tests do not replace hardware validation.

## Get Started

1. **Get firmware.** Download the `pico` UF2 for Raspberry Pi Pico (RP2040) or `pico2` for Raspberry Pi Pico 2
   (RP2350) from [Releases](https://github.com/djwinter29-oss/PicoPWM/releases), or build one with
   `tools/firmware/build.sh --board pico` (or `pico2`). The UF2 is written to `build/<board>/pico_pwm.uf2`.
2. **Flash the UF2.** Hold **BOOTSEL** while connecting the board, release it when the boot drive appears, then copy
   the downloaded or locally built `.uf2` onto that drive. The board reboots automatically.
3. **Wire your signals.** Follow the [hardware setup guide](docs/guides/hardware-setup.md) for the channel GPIO map,
   I2C pins and board-specific pin considerations.
4. **Connect to PicoPWM.** Open the board's USB CDC serial port in a terminal (see the
   [USB CDC shell guide](docs/guides/usb_cdc_shell.md)), or connect an I2C controller to the default 7-bit
   address `0x40`. Serial device names vary by OS; common examples are `/dev/ttyACM0` on Linux, `COMx` on Windows,
   and `/dev/cu.usbmodem*` on macOS. USB CDC does not require a particular terminal baud-rate setting.
5. **Verify.** In the CDC shell, run `board get version` to confirm the firmware is running, then
   `config get` to see the bank roles. See the [USB CDC shell guide](docs/guides/usb_cdc_shell.md) for command details.
6. **Open a dashboard (optional).** Use the [browser dashboard](https://djwinter29-oss.github.io/PicoPWM/) or run one
   locally as described below.

The default bank roles are monitor-only. To try a generator output, connect a 3.3 V-safe oscilloscope or logic
analyzer to GPIO 1 and GND, then configure Bank A and reboot:

```text
config set banka=generator
```

The command saves the target setting and reboots the board. After it reconnects, run `pwm set 0 1000 50` and
`pwm get 0` to request 1 kHz at 50% duty and inspect the realized output. When finished, run `pwm stop all` to stop
generator outputs; the saved bank role remains unchanged. See the [CDC shell guide](docs/guides/usb_cdc_shell.md)
for command details.

## Troubleshooting

### When an output stays low

1. Check that the bank is configured as a generator; monitor banks measure inputs and never drive outputs.
2. Check the realized state. A generator at `0 Hz / 0%` is intentionally held low, while `0 Hz / 100%` is held high.
3. Check the wiring: confirm the channel's GPIO in the [hardware setup guide](docs/guides/hardware-setup.md) and that
   the load shares ground with the board.

Use `config get` to inspect running and target bank roles and `pwm get <channel>` to inspect a channel's realized
frequency and duty; both are described in the [USB CDC shell guide](docs/guides/usb_cdc_shell.md).

## Dashboards

Use a dashboard for interactive control and monitoring. USB CDC and I2C remain available to scripts and other host
applications.

The source-checkout launchers below require a repository checkout. For a published Python release, install the wheel
with `python -m pip install picopwm-host`, start it with `picopwm-web`, then open <http://127.0.0.1:5000/>. For the
standalone .NET host, download and extract the `picopwm-host-v<VERSION>-<RID>` archive for your platform, then run
`./PicoPwm` (Linux/macOS) or `PicoPwm.exe` (Windows) from the extracted directory and open
<http://127.0.0.1:5000/>. In Windows PowerShell, invoke the executable as `.\PicoPwm.exe`. The .NET archive includes
its runtime. See
[Builds and Publishing](docs/guides/releases.md) for package availability and release details.

### Browser Dashboard

Open the [hosted dashboard](https://djwinter29-oss.github.io/PicoPWM/) in desktop Chrome or Edge, connect the board to
that computer, and select **Connect**. The browser talks to the board directly over WebHID; no local server is needed.
See the [WebHID guide](host/webhid/README.md) for browser requirements and controls.

### Local Python Dashboard

Run `tools/host/run-python.sh`, then open <http://127.0.0.1:5000/>. The Python server connects to the board over USB
HID. See the [Python guide](host/python/README.md) for configuration and API details.

### Local .NET Dashboard

Run `tools/host/run-dotnet.sh`, then open <http://127.0.0.1:5000/>. The .NET server also connects to the board over
USB HID. See the [.NET guide](host/dotnet/README.md) for configuration and API details.

### Linux HID Permissions

On Linux, configure HID access once with `tools/host/hid-permissions-setup.sh`; the Python and .NET launchers check
permissions before starting. For WebHID, run setup on the computer connected to the board, not only on a remote server.
See [Linux HID permissions](docs/guides/linux_hid_permissions.md).

## Hardware Compatibility

The `pico` and `pico2` profiles use the standard Pico and Pico 2 GPIO assignments documented in the
[hardware setup guide](docs/guides/hardware-setup.md). A third-party RP2040/RP2350 board is not automatically
compatible: verify it exposes the profile's channel GPIOs `0-22` and `28`; I2C additionally requires GPIOs `26` and
`27`. The standard profile also assumes the board's LED is on GPIO 25. Verify SWD access only if programming it with a
Debug Probe.
`tools/firmware/load.sh` is the developer workflow for programming a local build through a Debug Probe; see
[Program with a Pico Debug Probe](docs/guides/hardware-setup.md#program-with-a-pico-debug-probe). Its
`--flash-size <bytes>` option overrides OpenOCD's flash-size assumption only; it does not change the firmware's GPIO
mapping or board profile.

**Pico W and Pico 2 W** are not supported: the build helper and releases cover only the `pico` and `pico2` boards.

## Documentation

| Need | Guide |
| --- | --- |
| Wire and configure the board | [Hardware setup](docs/guides/hardware-setup.md), [Firmware configuration](docs/guides/configuration.md) |
| Control it | [USB CDC shell](docs/guides/usb_cdc_shell.md), [I2C protocol](docs/guides/i2c_protocol.md), [USB interface overview](docs/design/usb/usb-interface-overview.md) |
| Understand the design | [Architecture](docs/design/architecture.md), [PWM driver design](docs/design/pwm_driver_design.md), [generators](docs/design/generator/) and [monitors](docs/design/monitor/) |
| Write a HID host | [USB interface overview](docs/design/usb/usb-interface-overview.md), [HID report reference](docs/design/usb/hid-report-reference.md) |
| Build, test and release | [Build structure](BUILD_STRUCTURE.md), [Builds and publishing](docs/guides/releases.md) |
| Contribute | [Contributing](CONTRIBUTING.md), [Code of Conduct](CODE_OF_CONDUCT.md), [Security](SECURITY.md) |

The USB identity `0xCAFE:0x4012` is for development and lab use, not commercial derivatives. The CDC, HID and I2C
interfaces are unauthenticated; see [SECURITY.md](SECURITY.md). Build prerequisites, version handling and test commands
are in [BUILD_STRUCTURE.md](BUILD_STRUCTURE.md).

Related projects: [PicoUART](https://github.com/djwinter29-oss/PicoUart) (six USB serial ports) and
[PicoForge](https://github.com/djwinter29-oss/PicoForge) (host-controlled board services). They share no runtime
dependency.

Third-party software: the CDC shell uses [embedded-cli](https://github.com/funbiscuit/embedded-cli), included as the
`firmware/third_party/embedded-cli` submodule (v0.1.4, MIT).

## License

[BSD 3-Clause](LICENSE).
