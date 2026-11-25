# Contributing

Contributions to firmware, host tools, tests, and documentation are welcome. Most changes can be tested without
a board; validating electrical output, timing, and monitor accuracy requires hardware.

## Before You Start

- Read [README.md](README.md) and check existing issues and pull requests. Agree on scope in an issue before
  starting new features, design changes, or substantial fixes.
- For bugs, include reproduction steps, expected and actual behavior, versions, and relevant logs. For hardware
  issues, include the board, firmware, bank/backend configuration, and wiring.
- Follow the [Code of Conduct](CODE_OF_CONDUCT.md). Report suspected vulnerabilities privately through
  [SECURITY.md](SECURITY.md), not a public issue.

## Development Setup

Fork the repository on GitHub, then clone your fork and create a topic branch:

```sh
git clone https://github.com/YOUR-USERNAME/PicoPWM.git
cd PicoPWM
git remote add upstream https://github.com/djwinter29-oss/PicoPWM.git
git switch -c your-change
```

Run commands from the repository root. Use Linux or WSL2 Ubuntu for firmware development.
Host tests also run on macOS and Windows; shell commands require a POSIX shell such as Git Bash.
Python and .NET commands can also run directly in PowerShell.

Install only the toolchain needed for your change. Host-only work does not require the ARM firmware toolchain;
see the [Python](host/python/README.md), [.NET](host/dotnet/README.md), and [Direct Dashboard](host/direct/README.md) setup guides.

### Firmware Builds

On Ubuntu 24.04:

```sh
sudo apt-get update
sudo apt-get install --no-install-recommends \
    git build-essential cmake ninja-build python3 python3-venv python3-pip clang-format \
    gcc-arm-none-eabi libnewlib-arm-none-eabi \
    libstdc++-arm-none-eabi-dev libstdc++-arm-none-eabi-newlib
```

Build both supported boards:

```sh
./tools/firmware/build.sh --board pico
./tools/firmware/build.sh --board pico2
```

The helper initializes firmware submodules and writes `pico_pwm.uf2` and `pico_pwm.elf` to `build/<board>/`.
Fresh builds prefer Ninja; existing builds retain their cached generator. Set `CMAKE_GENERATOR` to override it,
using a fresh build directory when switching generators.

Local firmware defaults to `0.0.0-dev`; override with `--firmware-version x.y.z` (optional `-dev` or `-rcN`).
See [CI and Releases](docs/development/ci-and-releases.md) for CI and release packaging.

#### Program with a Pico Debug Probe

For local builds, `tools/firmware/load.sh` programs the ELF over SWD using OpenOCD. Connect the Debug Probe's three-pin
SWD cable to the target:

| Debug Probe signal | Cable color | Target signal |
|---|---|---|
| `SC` / SWCLK | Orange | SWCLK |
| GND | Black | GND |
| `SD` / SWDIO | Yellow | SWDIO |

Connect GND first to establish a common reference. Power the target board separately over USB; the SWD cable does not
power it. The Debug Probe uses 3.3 V I/O. For a third-party board, identify SWCLK, SWDIO, and GND from its schematic
rather than assuming the standard Pico connector placement. See the [Raspberry Pi Debug Probe guide](https://www.raspberrypi.com/documentation/microcontrollers/debug-probe.html)
for probe details.

The loader's `--flash-size <bytes>` option overrides OpenOCD's flash-size
assumption only; it does not change the firmware's GPIO mapping or board profile.

## Validate Your Change

Run the smallest relevant checks first, then broaden validation for shared behavior.

| Change | Validation |
| --- | --- |
| Firmware | `tools/test/test-firmware-c.sh`, `tools/test/check-firmware-invariants.sh`, the formatting checks below, and builds for both `pico` and `pico2` with `tools/firmware/build.sh`. |
| Python, .NET, or browser tools | See [Host App Development](docs/development/host-apps.md) for focused commands; run `tools/test/test-host.sh` for shared host changes. |
| Scripts and tooling | Run the relevant `tools/test/syntax-check-python.sh` or `tools/test/syntax-check-shell.sh`; for firmware build/load helper changes, run `python3 -m unittest discover -s tools/test -p 'test_firmware_*.py'`. |
| Documentation only | Check links, examples, and rendered formatting; run `git diff --check`. |

Before running native C tests without a firmware build, initialize their source dependency:

```sh
git submodule update --init firmware/third_party/embedded-cli
tools/test/test-firmware-c.sh
```

### C Formatting

Check each edited C file, for example:

```sh
clang-format --dry-run --Werror --style=file firmware/src/control_api.c
```

To format, use `clang-format -i --style=file <file>` and inspect the diff.
For committed PR changes, compare against the target branch:

```sh
git fetch upstream main
tools/test/check-clang-format.sh --base "$(git merge-base upstream/main HEAD)" --head HEAD
```

The script checks changed line ranges, not whole files or uncommitted edits. Without arguments, it checks the last commit.

### Host Tests

Per-application setup, focused test commands, and coverage limits are in
[Host App Development](docs/development/host-apps.md).

The aggregate runner requires Python test dependencies, Node.js, .NET, and a POSIX shell.
With the Python environment active, run `PYTHON=python tools/test/test-host.sh`.
See the [host validation workflow](.github/workflows/host-validation.yml) for CI's platform setup.

### Hardware Validation

For PWM, CDC, or HIL tooling changes, also run the hardware-free regressions:

```sh
python -m unittest discover -s tools/hil/tests -p 'test_*.py'
```

For physical testing, follow the [HIL connection guide](docs/validation/hil/connections.md) and
[test plan](docs/validation/hil/test-plan.md). See the [runner instructions](tools/hil/README.md) for setup
and invocation. Hardware tests require operator authorization; report missing hardware or confirmation as not
run. Host tests and the CDC subset are not full hardware qualification.

## Documentation

- Use kebab-case filenames. Put operating instructions in `docs/guides/`, host-facing contracts in
  `docs/protocols/`, firmware internals in `docs/design/`, host-app development and releases in
  `docs/development/`, and hardware validation in `docs/validation/`.
- Link to authoritative pages rather than duplicating details. Update the [index](docs/README.md) and affected
  links when adding, moving, or renaming pages.

## Pull Requests

- Push your topic branch to your fork and open a pull request against `djwinter29-oss/PicoPWM`'s `main` branch.
- Keep changes focused; follow the [coding conventions](.github/copilot-instructions.md), including C Doxygen rules
  and [source-file licensing requirements](.github/copilot-instructions.md#licensing). New first-party source files
  need the prescribed SPDX/copyright notices; preserve third-party licenses.
- Respect the core split: Core 0 owns CDC, I2C and HID; Core 1 owns PWM backends, and
  `firmware/src/driver/pwm/pwm_driver.c` is the multicore boundary.
- Add regression tests for non-trivial changes and update matching docs. Consult the
  [architecture](docs/design/architecture.md) and [PWM driver design](docs/design/pwm/driver.md).
- Explain the problem, solution, compatibility impacts (I2C, HID, flash layout), and related issue.
- List checks and results, including skipped checks and reasons.
