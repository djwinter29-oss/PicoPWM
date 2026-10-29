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

- Firmware builds, versioning and test commands: [BUILD_STRUCTURE.md](BUILD_STRUCTURE.md).
- Architecture and driver internals: [docs/design/architecture.md](docs/design/architecture.md) and
  [docs/design/pwm-driver/driver-design.md](docs/design/pwm-driver/driver-design.md).
- Host tools: [host/python](host/python/README.md), [host/dotnet](host/dotnet/README.md) and
  [host/webhid](host/webhid/README.md).

Use Linux or WSL2 Ubuntu for firmware development. Host tests also run on macOS and Windows in CI.

## Validate Your Change

Run the smallest relevant checks first, then broaden validation for shared behavior.

| Change | Validation |
| --- | --- |
| Firmware | `tools/test/test-firmware-c.sh`, `tools/test/check-firmware-invariants.sh`, `tools/test/check-clang-format.sh`, and builds for both `pico` and `pico2` with `tools/firmware/build.sh`. |
| Python, .NET, or browser tools | `tools/test/test-host.sh`, or the tests of the affected host only. |
| Documentation only | Check links, examples, and rendered formatting; run `git diff --check`. |

Hardware is not required for every pull request. If hardware is unavailable, report that limitation rather than
claiming hardware validation.

## Pull Requests

- Keep changes small and focused; follow the [coding conventions](.github/copilot-instructions.md), including the
  Doxygen requirements for C.
- Respect the core split: Core 0 owns CDC, I2C and HID; Core 1 owns PWM backends, and
  `firmware/src/driver/pwm/pwm_driver.c` is the multicore boundary.
- Add a focused regression test for non-trivial fixes or new behavior.
- Update matching docs when behavior changes; do not duplicate content across docs.
- Explain the problem, the change, and any compatibility impact (for example I2C, HID or flash layout changes); link
  the related issue when applicable.
- List checks run and their results, and identify skipped checks and why they were skipped.

## Reporting Security Issues

Do not open a public issue for suspected vulnerabilities. Follow [SECURITY.md](SECURITY.md) instead.

## Code of Conduct

This project follows the [Code of Conduct](CODE_OF_CONDUCT.md).
