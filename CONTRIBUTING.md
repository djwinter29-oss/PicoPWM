# Contributing

Contributions to firmware, host tools, tests, and documentation are welcome. You do not need a board for many changes;
validating PWM output, timing and monitor accuracy requires hardware.

## Before You Start

- Read [README.md](README.md) for project scope and the [documentation index](docs/README.md).
- Check open issues and pull requests to avoid duplicate work.
- For new features, design changes, or substantial fixes, open an issue to agree on scope before starting.
- When reporting a bug, include reproduction steps, expected and actual behavior, and relevant versions and logs.
  For hardware issues, also include the board, firmware version, backend and bank configuration, and wiring.

## Development Setup

- Firmware build and test instructions are below; CI and release workflows are described in
  [Builds and Publishing](docs/deployment/releases.md).
- Architecture and driver internals: [docs/design/architecture.md](docs/design/architecture.md) and
  [docs/design/pwm-driver/driver-design.md](docs/design/pwm-driver/driver-design.md).
- Host tools: [host/python](host/python/README.md), [host/dotnet](host/dotnet/README.md) and
  [host/webhid](host/webhid/README.md).

Use Linux or WSL2 Ubuntu for firmware development. Host tests also run on macOS and Windows in CI.
Run the commands below from the repository root. Shell scripts require a POSIX shell; on Windows, use Git Bash
for the aggregate host runner or run the focused Python and .NET commands directly in PowerShell.

### Firmware Builds

On Ubuntu 24.04, install the native build tools, Python, ARM cross-toolchain and libraries, and formatting tools:

```sh
sudo apt-get update
sudo apt-get install --no-install-recommends \
    git build-essential cmake ninja-build python3 python3-venv python3-pip clang-format \
    gcc-arm-none-eabi libnewlib-arm-none-eabi \
    libstdc++-arm-none-eabi-dev libstdc++-arm-none-eabi-newlib
```

The ARM packages match CI's firmware dependencies; native compilers are needed for host-side C tests and SDK tools.
Then build both supported boards:

```sh
./tools/firmware/build.sh --board pico
./tools/firmware/build.sh --board pico2
```

The helper initializes the `firmware/third_party` submodules, including the pinned Pico SDK, and writes outputs to
`build/<board>/`. Fresh builds prefer Ninja when it is available; otherwise CMake uses its platform default. Existing
build directories keep their cached generator. Set `CMAKE_GENERATOR` to choose one explicitly; switching an existing
directory to another generator requires a fresh build directory. The helper does not delete build state.

The UF2 for USB flashing is `build/<board>/pico_pwm.uf2`; `pico_pwm.elf` is available for debug tools. Local firmware
builds use version `0.0.0-dev`; pass `--firmware-version x.y.z` with an optional `-dev` or `-rcN` suffix to override
it. Release versions and packaging are covered in [Builds and Publishing](docs/deployment/releases.md).

## Documentation

- Put user task guidance in `docs/guides/`, implementation details and protocol
  contracts in `docs/design/`, release workflows in `docs/deployment/`, and
  validation material in `docs/tests/`.
- Use kebab-case for documentation filenames.
- Link to authoritative pages instead of duplicating detailed instructions or
  tables.
- Update the [documentation index](docs/README.md) and affected links when
  adding, moving, or renaming a page.

## Validate Your Change

Run the smallest relevant checks first, then broaden validation for shared behavior.

| Change | Validation |
| --- | --- |
| Firmware | `tools/test/test-firmware-c.sh`, `tools/test/check-firmware-invariants.sh`, the formatting checks below, and builds for both `pico` and `pico2` with `tools/firmware/build.sh`. |
| Python, .NET, or browser tools | Use the focused commands below; run `tools/test/test-host.sh` for shared host changes. |
| Documentation only | Check links, examples, and rendered formatting; run `git diff --check`. |

Before running native C tests without a firmware build, initialize their source dependency:

```sh
git submodule update --init firmware/third_party/embedded-cli
tools/test/test-firmware-c.sh
```

### C Formatting

For edited files before committing, check each affected file explicitly, for example:

```sh
clang-format --dry-run --Werror --style=file firmware/src/control_api.c
```

Use `clang-format -i --style=file <file>` to format a file, and inspect the resulting diff.
For committed PR changes, fetch the target branch and select the PR's merge base:

```sh
git fetch origin main
tools/test/check-clang-format.sh --base "$(git merge-base origin/main HEAD)" --head HEAD
```

The script checks changed line ranges, not whole files. Without arguments it selects the last commit's ranges;
it does not select uncommitted changes or the entire PR automatically.

### Focused Host Tests

- **Python:** use Python 3.10+ and an activated virtual environment (see the
  [Python guide](host/python/README.md#quick-start)). Install test dependencies with
  `python -m pip install --editable "host/python[test]"`, then run `python -m pytest -q host/python/tests`.
  CI also installs `libhidapi-dev` on Linux or `hidapi` via Homebrew on macOS.
- **Browser:** use Node.js 22, matching CI, and run
  `node --test host/web/tests/*.test.mjs host/webhid/tests/*.test.mjs` in a POSIX shell.
- **.NET:** install the .NET 10 SDK and run
  `dotnet test host/dotnet/PicoPwm.sln --configuration Release --nologo`.
- **Host launchers:** run `python tools/test/test_run_python.py` and
  `python tools/test/test_run_dotnet.py`.

The aggregate runner requires all three toolchains, the Python test dependencies, and a POSIX shell.
Activate the Python environment first; use `PYTHON=python tools/test/test-host.sh` to select its interpreter.
CI's exact versions and platform setup are in [Host Validation](.github/workflows/host-validation.yml).

### Hardware Validation

For PWM generation/monitoring, CDC commands, or HIL tooling changes, also run the hardware-free runner regressions:

```sh
python -m unittest discover -s tools/hil/tests -p 'test_*.py'
```

Physical testing requires the [HIL connection guide](docs/tests/hil-test-connections.md) and
[test plan](docs/tests/hil-test-plan.md). Before running the CDC subset, the operator must confirm the port, board,
actual flashed artifact, fixture, and permission to drive outputs. Require exactly one running generator bank and
two monitor banks; do not automatically change roles, save configuration, reboot, or rewire to make a run possible.
The test plan documents the launcher and its required `--confirm-fixture` option.

The runner replaces `docs/tests/records/latest.md`; preserve an existing record under a dated filename first if
needed. See the [record guidance](docs/tests/records/README.md). Inspect failures, unexecuted checks, and cleanup
status. A nonzero exit is a failure.
The CDC subset does not qualify instrument accuracy, other transports, or the remaining generator-bank passes.
If hardware or operator confirmation is unavailable, report HIL as not run and explain why; host tests are not
hardware qualification.

## Pull Requests

- Keep changes small and focused; follow the [coding conventions](.github/copilot-instructions.md), including the
  Doxygen requirements for C.
- Respect the core split: Core 0 owns CDC, I2C and HID; Core 1 owns PWM backends, and
  `firmware/src/driver/pwm/pwm_driver.c` is the multicore boundary.
- Add a focused regression test for non-trivial fixes or new behavior.
- Update matching docs when behavior changes.
- Explain the problem, the change, and any compatibility impact (for example I2C, HID or flash layout changes); link
  the related issue when applicable.
- List checks run and their results, and identify skipped checks and why they were skipped.

## Reporting Security Issues

Do not open a public issue for suspected vulnerabilities. Follow [SECURITY.md](SECURITY.md) instead.

## Code of Conduct

This project follows the [Code of Conduct](CODE_OF_CONDUCT.md).
