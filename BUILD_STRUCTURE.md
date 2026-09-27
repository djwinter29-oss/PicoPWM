# Build Directory Structure

All build artifacts are now consolidated under `firmware/build/` and are git-ignored.

## Directory Layout

```
PicoPWM/
├── firmware/
│   ├── build/                 # ← All builds here (git-ignored)
│   │   ├── generator/
│   │   ├── monitor/
│   │   ├── pico/              # Board-specific
│   │   ├── pico2/             # Board-specific
│   │   └── custom/            # Custom profiles
│   ├── src/
│   └── CMakeLists.txt
├── .gitignore                 # Ignores /firmware/build-*
└── tools/firmware/
    └── build.sh               # Updated to support unified structure
```

## Usage

### Default (400 kHz I2C at address 0x40, creates `firmware/build/`)
```bash
PICO_SDK_PATH=/path/to/pico-sdk ./tools/firmware/build.sh
```

### Default with Named Build Directory (400 kHz I2C at address 0x40)
```bash
PICO_SDK_PATH=/path/to/pico-sdk ./tools/firmware/build.sh --profile my-build
```

Note: `--profile` here only names the output directory
(`firmware/build/<name>`); channel roles (generator/monitor per bank) are no
longer selected at build time, see
[Firmware Configuration](docs/configuration.md#runtime-bank-locking).

### Custom I2C Clock Speed
The I2C slave is configured for **400 kHz by default**, which is backward-compatible with 100 kHz masters.

To use 100 kHz (Standard I2C):
```bash
PICO_SDK_PATH=/path/to/pico-sdk cmake -S firmware -B firmware/build/gen-100k \
  -DPICO_PWM_I2C_CLOCK_SPEED=100000

cmake --build firmware/build/gen-100k --parallel
```

Supported speeds:
- `100000` - Standard I2C (100 kHz)
- `400000` - Fast I2C (400 kHz, default, backward-compatible with 100 kHz masters)

### Custom I2C Address
The default I2C slave address is **0x40**. Change it through CDC or I2C:

```text
config address 0x50
config save
reboot
```

Valid user-configurable 7-bit addresses: `0x08` to `0x77`; reserved I2C
addresses are rejected.

### Build-time clock and runtime address

I2C clock speed is selected when configuring the firmware. The slave address is
not a CMake option; change it after flashing.

```bash
PICO_SDK_PATH=/path/to/pico-sdk cmake -S firmware -B firmware/build/i2c-100k \
  -DPICO_PWM_I2C_CLOCK_SPEED=100000
cmake --build firmware/build/i2c-100k --parallel
```

Then set the address from the CDC shell and reboot so the saved target is applied:

```text
config address 0x30
config save
reboot
```

## Git Ignore Rules

Updated `.gitignore` includes:
```
/firmware/build/
/firmware/build-*/
/firmware/build-coverage/
```

This ensures all CMake build artifacts stay out of version control.

## Benefits

- ✅ Single, organized build folder (no scattered `build-*` directories)
- ✅ Profile/board variants organized as subdirectories
- ✅ Git automatically ignores all build artifacts
- ✅ Cleaner workspace, easier to clean with `rm -rf firmware/build/`
- ✅ Backward compatible (existing `--build-dir` option still works)
