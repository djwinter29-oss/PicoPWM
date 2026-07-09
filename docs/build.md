# Build & Flash

This guide covers building the PicoPWM firmware and flashing it to a Raspberry Pi Pico or Pico 2.

PicoPWM is intended to support two firmware roles:

- **PWM generator** — the normal build used to generate 24 logical PWM outputs.
- **PWM monitoring** — a monitoring-oriented build that keeps the same external pin layout.

The current repository build flow below produces the standard firmware image name `pico_pwm`. When adding a monitoring target, keep the same GPIO/channel layout documented in [hardware.md](hardware.md).

---

## Prerequisites

### 1. Raspberry Pi Pico SDK

Clone the SDK and set the `PICO_SDK_PATH` environment variable.

**Windows (Git Bash or PowerShell)**

```bash
cd C:\
git clone https://github.com/raspberrypi/pico-sdk.git
cd pico-sdk
git submodule update --init
set PICO_SDK_PATH=C:\pico-sdk
```

**Linux / macOS**

```bash
cd ~
git clone https://github.com/raspberrypi/pico-sdk.git
cd pico-sdk
git submodule update --init
export PICO_SDK_PATH=$HOME/pico-sdk
```

> Use a recent Pico SDK release with support for both RP2040 and RP2350 targets.

### 2. Build Tools

| Tool | Windows | Linux | macOS |
|------|---------|-------|-------|
| CMake | `cmake` from installer or winget | `sudo apt install cmake` | `brew install cmake` |
| Make / MinGW | `mingw32-make` or `make` | `make` | `make` |
| GCC Arm | `arm-none-eabi-gcc` from Arm GNU Toolchain | `gcc-arm-none-eabi` | `brew install gcc-arm-embedded` |
| Python | 3.8+ | 3.8+ | 3.8+ |

Make sure `arm-none-eabi-gcc` is on your PATH.

---

## Build Steps

### 1. Open a Terminal in the Project Directory

```bash
cd C:\Users\ji\Documents\PicoPWM\firmware
```

### 2. Create a Build Directory

```bash
mkdir build
cd build
```

### 3. Run CMake

**Windows with MinGW**

```bash
cmake -DPICO_SDK_PATH=C:\pico-sdk -G "MinGW Makefiles" ..
```

If you prefer Ninja:

```bash
cmake -DPICO_SDK_PATH=C:\pico-sdk -G Ninja ..
```

**Linux / macOS**

```bash
cmake -DPICO_SDK_PATH=$PICO_SDK_PATH ..
```

### 4. Build

```bash
make -j4
```

On Windows with MinGW, you may need:

```bash
mingw32-make -j4
```

If the build succeeds, you will see:

```text
[100%] Linking CXX executable pico_pwm.elf
```

### 5. Build Outputs

The build directory will contain:

| File | Description |
|------|-------------|
| `pico_pwm.elf` | ELF binary for debugging |
| `pico_pwm.uf2` | UF2 file for flashing via USB |
| `pico_pwm.hex` | Intel HEX for some debuggers |
| `pico_pwm.map` | Memory map |

---

## Flashing the Firmware

### Method 1: USB Mass Storage (BOOTSEL)

1. Hold the **BOOTSEL** button on the Pico.
2. Plug the Pico into USB via a cable.
3. Release the BOOTSEL button.
4. The board appears as a USB drive named `RPI-RP2`.
5. Copy `pico_pwm.uf2` to that drive.
6. The Pico automatically reboots and runs the new firmware.

### Method 2: Using `picotool`

If you have `picotool` installed:

```bash
picotool load pico_pwm.uf2
picotool reboot
```

### Method 3: Using a Debug Probe (SWD)

If you have a Picoprobe or other CMSIS-DAP probe:

```bash
openocd -f interface/cmsis-dap.cfg -f target/rp2040.cfg -c "program pico_pwm.elf verify reset exit"
```

For Pico 2 / RP2350, use the matching OpenOCD target script provided by your OpenOCD installation.

---

## Connecting to the USB CDC Serial Port

After flashing, the Pico enumerates as a USB CDC serial device.

### Windows

1. Open Device Manager.
2. Find `Ports (COM & LPT)` → note the COM port (e.g., `COM3`).
3. Open a terminal:

```bash
mode COM3:115200,n,8,1
```

Or use PuTTY, Tera Term, or VS Code Serial Monitor at 115200 baud.

### Linux

```bash
ls /dev/ttyACM*
sudo minicom -D /dev/ttyACM0 -b 115200
```

### macOS

```bash
ls /dev/tty.usbmodem*
screen /dev/tty.usbmodem0001 115200
```

Type `help` and press Enter to see the command list.

---

## I2C Verification

Use an I2C master (Arduino, Raspberry Pi, logic analyzer, or USB-I2C adapter) to scan the bus. You should see device `0x40`.

Example with `i2cdetect` on Linux:

```bash
sudo i2cdetect -y 1
```

You should see `40` in the address map.

---

## Troubleshooting

### `PICO_SDK_PATH not found`

Make sure the environment variable is set and CMake can see it:

```bash
echo %PICO_SDK_PATH%        # Windows
echo $PICO_SDK_PATH         # Linux/macOS
```

### `arm-none-eabi-gcc not found`

Add the Arm toolchain `bin` directory to your PATH.

### Build fails with USB/TinyUSB errors

Make sure the Pico SDK submodules are fully initialized:

```bash
cd %PICO_SDK_PATH%
git submodule update --init --recursive
```

### Pico does not appear as CDC serial port

- The first flash after BOOTSEL may take a few seconds to enumerate.
- Try unplugging and reconnecting the USB cable.
- Check that your USB cable supports data (not charge-only).

### I2C does not respond

- Confirm 4.7 kΩ pull-up resistors from SDA/SCL to 3.3 V.
- Confirm the master is using 7-bit address `0x40` (not `0x80`).
- Confirm the master does not clock faster than the slave can handle (start with 100 kHz).

---

## Next Steps

- Read the [Control Protocol](protocol.md) to learn how to communicate with the firmware.
- Read the [Hardware Notes](hardware.md) for wiring details.
- Read the [Internal API](api.md) if you want to modify the firmware.
