# Build Directory Structure

All firmware, host-test, and coverage build artifacts are organized under the
repository-root `build/` directory, which is git-ignored.

## Directory Layout

```text
PicoPWM/
├── build/
│   ├── pico/                 # Firmware build for Raspberry Pi Pico
│   ├── pico2/                # Firmware build for Raspberry Pi Pico 2
│   ├── tests/                # Host-side firmware C tests
│   └── tests-coverage/       # Host tests and coverage reports
├── firmware/
│   ├── src/
│   └── CMakeLists.txt
└── tools/
    ├── firmware/
    └── test/
```

## Firmware Builds

For CI artifacts, release tags, manual test publishing and repository setup, see
[Builds and Publishing](docs/guides/releases.md).

The helper builds into `build/<board>` and defaults to `pico`. The Pico SDK
is always the `firmware/third_party/pico-sdk` submodule. Fresh builds prefer Ninja
when `ninja` is on `PATH`; otherwise CMake uses its platform default. Existing
build directories retain their cached generator. Set `CMAKE_GENERATOR` to
explicitly pick a generator:

```sh
./tools/firmware/build.sh --board pico
./tools/firmware/build.sh --board pico2
CMAKE_GENERATOR=Ninja ./tools/firmware/build.sh --board pico
```

To switch an existing Makefiles build to Ninja, move `build/<board>` aside first
so CMake configures a fresh build directory. The helper does not delete build state.

Prerequisites: `git`, `cmake`, a build tool and `arm-none-eabi-gcc` on `PATH`. The helper initializes the
`firmware/third_party` submodules, including the pinned Pico SDK.

Check the helper's generator selection without compiling firmware:
`python3 tools/test/test_firmware_build.py`.

Outputs: `build/<board>/pico_pwm.uf2` for USB flashing and `pico_pwm.elf` for debug tools. Other flash options are
`picotool load pico_pwm.uf2` or SWD/OpenOCD with the `.elf`.

Versioning: local builds use `0.0.0-dev`. Pass `--firmware-version x.y.z` (optional `-dev` or `-rcN` suffix) to
override it; the release workflow builds tags `vx.y.z` with `x.y.z`. The version feeds the `version` CDC command,
the I2C `REG_VERSION` register, HID identity and the USB `bcdDevice`. See `firmware/version.cmake`.

Channel roles are selected in the startup configuration. See
[Firmware Configuration](docs/guides/configuration.md#startup-bank-configuration).

Host-side firmware tests use the same root build directory:

```sh
tools/test/test-firmware-c.sh
tools/test/coverage-firmware-c.sh
tools/test/test-host.sh   # Python, web/WebHID and .NET host tests
```

## I2C Slave

The I2C slave timing is fixed at 400 kHz in `firmware/src/config/board.c`, which is
backward-compatible with 100 kHz masters.

The default I2C address is `0x40`. Change it through CDC or I2C and reboot to
apply the saved address:

```text
config address 0x50
config save
reboot
```

Valid user-configurable 7-bit addresses are `0x08` to `0x77`; reserved I2C
addresses are rejected.

## Git Ignore Rules

The root `build/` directory is ignored. The legacy `firmware/build/` ignore
remains to avoid tracking artifacts created by earlier project versions.
