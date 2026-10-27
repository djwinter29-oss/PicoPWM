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

The helper builds into `build/<board>` and defaults to `pico`. The Pico SDK
is always the `firmware/third_party/pico-sdk` submodule; set `CMAKE_GENERATOR` to
pick a generator:

```sh
./tools/firmware/build.sh --board pico
./tools/firmware/build.sh --board pico2
```

Channel roles are selected in the startup configuration. See
[Firmware Configuration](docs/guides/configuration.md#startup-bank-configuration).

Host-side firmware tests use the same root build directory:

```sh
tools/test/test-firmware-c.sh
tools/test/coverage-firmware-c.sh
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
