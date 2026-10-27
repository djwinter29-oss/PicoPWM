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

The helper defaults to `build/`; selecting a board or profile places that
build in a named subdirectory:

```sh
./tools/firmware/build.sh --board pico
./tools/firmware/build.sh --board pico2
./tools/firmware/build.sh --profile my-build
```

These commands produce build directories under `build/`. The `--profile`
option only names a build directory; channel roles are selected in the startup
configuration. See
[Firmware Configuration](docs/guides/configuration.md#startup-bank-configuration).

Host-side firmware tests use the same root build directory:

```sh
tools/test/test-firmware-c.sh
tools/test/coverage-firmware-c.sh
```

## Custom I2C Clock Speed

The I2C slave is configured for **400 kHz by default**, which is
backward-compatible with 100 kHz masters. To use 100 kHz:

```sh
PICO_SDK_PATH="$PWD/firmware/third_party/pico-sdk" \
  cmake -S firmware -B build/i2c-100k -DPICO_PWM_I2C_CLOCK_SPEED=100000
cmake --build build/i2c-100k --parallel
```

Supported speeds:

- `100000` - Standard I2C (100 kHz)
- `400000` - Fast I2C (400 kHz, default, backward-compatible with 100 kHz masters)

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
