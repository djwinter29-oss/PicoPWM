# PicoPWM

[![PR Check](https://github.com/djwinter29-oss/PicoPWM/actions/workflows/pr-test.yml/badge.svg?branch=main)](https://github.com/djwinter29-oss/PicoPWM/actions/workflows/pr-test.yml)
[![Release](https://github.com/djwinter29-oss/PicoPWM/actions/workflows/release.yml/badge.svg)](https://github.com/djwinter29-oss/PicoPWM/actions/workflows/release.yml)
[![License: BSD 3-Clause](https://img.shields.io/badge/License-BSD%203--Clause-blue.svg)](LICENSE)
![Targets: RP2040 and RP2350](https://img.shields.io/badge/Targets-RP2040%20%7C%20RP2350-2674b8)

**24 PWM channels. One Pico. Three ways to control it.** PicoPWM turns a Raspberry Pi Pico or Pico 2 into a PWM
generator and frequency monitor, controlled through a USB serial CLI, I2C, or USB HID clients and dashboards.
The firmware exposes three fixed 8-channel banks. Configure each bank as a generator or monitor; channel IDs stay the same across control interfaces.

**Quick links:** [Download firmware](https://github.com/djwinter29-oss/PicoPWM/releases) ·
[Hardware setup](docs/guides/hardware-setup.md) ·
[Open dashboard](https://djwinter29-oss.github.io/PicoPWM/) ·
[Python package](https://pypi.org/project/picopwm/)

## Why PicoPWM?

- **Simple serial CLI:** control outputs and read channel state from a USB serial terminal.
- **Easy test automation:** integrate PWM generation and monitoring into test scripts using the CLI, Python HID
  client, or I2C. Start with the [Python scripting example](host/python/README.md#use-from-python).
- **Ready-to-use dashboards:** control and monitor channels through a browser, Python, or standalone .NET application.
- **24 flexible channels:** three eight-channel banks using hardware, PIO, and software PWM, each configurable as
  generator or monitor.

![WebHID dashboard with simulated device identity, boot configuration, runtime status, and 24 PWM generator and monitor channels.](docs/assets/pico-pwm-dashboard.png)

_Illustrative dashboard with simulated readings, not a live hardware result. Python and .NET share this layout._

## Get Started

**Supported boards:** Raspberry Pi Pico (`pico`, RP2040) and Pico 2 (`pico2`, RP2350).
Pico W and Pico 2 W are not supported; [third-party boards need compatibility checks](docs/guides/hardware-setup.md#board-compatibility).

Before wiring:

- GPIO signals are **3.3 V only**; never connect 5 V signals or tie generator outputs together.
- Banks default to **monitor-only**. Generator banks start at 0 Hz / 0% duty.
- Use the default system clock; higher profiles may overclock the board and are not qualified for every board.
- For I2C, add external SDA/SCL pull-ups and start at 100 kHz (default address `0x40`).

1. **Flash firmware.** Download your board's UF2 from [Releases](https://github.com/djwinter29-oss/PicoPWM/releases).
   Hold **BOOTSEL** while connecting USB, then copy the UF2 to the boot drive.
2. **Connect.** Follow the [wiring guide](docs/guides/hardware-setup.md), then open the board's
   [USB CDC shell](docs/guides/usb-cdc-shell.md) in a serial terminal.
3. **Verify.** Run `board get version` and `config get`.

To try 1 kHz / 50% output, connect a 3.3 V-safe oscilloscope or logic analyzer to **GPIO 1 and GND**, then run:

```text
config set banka=generator
```

This saves the setting and reboots automatically. Reconnect, then run:

```text
pwm set 0 1000 50
pwm get 0
```

Illustrative readback (realized values can vary with timing quantization):

```text
CH0: freq=1000 Hz, duty=50%, enabled=yes
```

This is firmware readback, not an independent electrical measurement.
**When finished**, stop all generator outputs:

```text
pwm stop all
```

Stopping does not change saved bank roles; Bank A remains configured as a generator after reboot. To restore
monitor-only operation, see [Configure bank roles](docs/guides/setup-and-troubleshooting.md#configure-bank-roles).
For configuration and connection issues, see [Setup and troubleshooting](docs/guides/setup-and-troubleshooting.md).

## Recommended Frequency Ranges

Conservative starting points, **not hardware-qualified accuracy specifications**:

| Backend | Generation | Measurement |
| --- | --- | --- |
| [Hardware PWM](docs/design/pwm-driver/hardware.md) (Bank A) | 15 Hz-1 MHz | 100 Hz-1 MHz |
| [PIO PWM](docs/design/pwm-driver/pio.md) (Bank B) | 1 Hz-100 kHz | 10 Hz-100 kHz |
| [Software PWM](docs/design/pwm-driver/software.md) (Bank C) | 1 Hz-1 kHz | 10 Hz-1 kHz |

Use moderate duty cycles; narrow pulses and timing quantization reduce usable ranges.
Generation accepts integer 0%-100% duty; `0 Hz` selects static low (`0%`) or high (`100%`).
**PIO duty readback can differ from the physical signal**, especially at high frequencies;
see the [timing caveats](docs/design/pwm-driver/pio.md#frequency-versus-duty-resolution).
PIO/software measurement below 10 Hz is experimental; see the [reported low-frequency issue](docs/design/pwm-driver/software.md#low-frequency-loopback-observation).

Manually tested on a Pico HIL fixture: each bank in turn generated on all eight channels at 100, 500 and
1000 Hz with 25%, 50% and 75% duty, and monitor readback was within ±5% frequency and ±5 points duty
([record](docs/tests/records/30-10-2026-manual-pico-bank-rotation.md)). This establishes functional operation in the tested
setup, not accuracy across all boards and ranges. Verify frequency and duty with an independent instrument;
host tests and loopback readback do not establish accuracy. Backend links above cover extended ranges and limitations.

## Dashboards

| Option | Start here |
| --- | --- |
| Browser (WebHID) | [Open dashboard](https://djwinter29-oss.github.io/PicoPWM/) in desktop Chrome or Edge; select **Connect**. [Guide](host/webhid/README.md). |
| Python 3.10+ | [Install and run `picopwm`](host/python/README.md#quick-start) from PyPI. |
| Standalone .NET | [Download and run](host/dotnet/README.md#standalone-application) a release package; no separate .NET installation needed. |

Connect the board to the computer running the browser or host application and grant browser permission when prompted.
Linux may require [HID permission setup](docs/guides/setup-and-troubleshooting.md#linux-hid-access).

## Documentation

See the [documentation index](docs/README.md) for the full map.

| Need | Resource |
| --- | --- |
| Wire, configure, and troubleshoot | [Hardware setup](docs/guides/hardware-setup.md), [Setup and troubleshooting](docs/guides/setup-and-troubleshooting.md) |
| Control it | [USB CDC shell](docs/guides/usb-cdc-shell.md), [I2C protocol](docs/design/i2c/i2c-protocol.md), [USB interface overview](docs/design/usb/usb-interface-overview.md) |
| Understand the design | [Architecture](docs/design/architecture.md), [PWM driver design](docs/design/pwm-driver/driver-design.md) |
| Write a HID host | [HID report reference](docs/design/usb/hid-report-reference.md) |
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

First-party code: [BSD 3-Clause](LICENSE), copyright (c) 2026 Ji Dong.
Third-party dependencies retain their own licenses.
