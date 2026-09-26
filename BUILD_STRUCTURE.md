# Build Directory Structure

All build artifacts are now consolidated under `firmware/build/` and are git-ignored.

## Directory Layout

```
PicoPWM/
├── firmware/
│   ├── build/                 # ← All builds here (git-ignored)
│   │   ├── generator/
│   │   ├── monitor/
│   │   ├── software_generator/
│   │   ├── software_monitor/
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

### Default (400 kHz I2C, creates `firmware/build/`)
```bash
PICO_SDK_PATH=/path/to/pico-sdk ./tools/firmware/build.sh
```

### Default with Profile (400 kHz I2C)
```bash
PICO_SDK_PATH=/path/to/pico-sdk ./tools/firmware/build.sh --profile generator
PICO_SDK_PATH=/path/to/pico-sdk ./tools/firmware/build.sh --profile monitor
```

### Custom I2C Clock Speed
The I2C slave is configured for **400 kHz by default**, which is backward-compatible with 100 kHz masters.

To use 100 kHz (Standard I2C):
```bash
PICO_SDK_PATH=/path/to/pico-sdk cmake -S firmware -B firmware/build/gen-100k \
  -DPICO_PWM_PROFILE=generator \
  -DPICO_PWM_I2C_CLOCK_SPEED=100000

cmake --build firmware/build/gen-100k --parallel
```

Supported speeds:
- `100000` - Standard I2C (100 kHz)
- `400000` - Fast I2C (400 kHz, default, backward-compatible with 100 kHz masters)

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
