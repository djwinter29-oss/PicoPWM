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

### Firmware Builds

Install `git`, `cmake`, a build tool, and `arm-none-eabi-gcc`, then build either supported board:

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
- Update matching docs when behavior changes.
- Explain the problem, the change, and any compatibility impact (for example I2C, HID or flash layout changes); link
  the related issue when applicable.
- List checks run and their results, and identify skipped checks and why they were skipped.

## Reporting Security Issues

Do not open a public issue for suspected vulnerabilities. Follow [SECURITY.md](SECURITY.md) instead.

## Code of Conduct

This project follows the [Code of Conduct](CODE_OF_CONDUCT.md).
