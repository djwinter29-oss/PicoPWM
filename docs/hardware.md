# Hardware Notes

This page documents the GPIO pinout, reserved pins, electrical considerations, and wiring recommendations for PicoPWM.

## GPIO Pinout

| GPIO | Function | Notes |
|------|----------|-------|
| 0 | HW PWM ch 0 | Slice 0, channel A |
| 1 | SW PWM ch 0 | |
| 2 | HW PWM ch 1 | Slice 1, channel A |
| 3 | SW PWM ch 1 | |
| 4 | HW PWM ch 2 | Slice 2, channel A |
| 5 | SW PWM ch 2 | |
| 6 | HW PWM ch 3 | Slice 3, channel A |
| 7 | SW PWM ch 3 | |
| 8 | HW PWM ch 4 | Slice 4, channel A |
| 9 | SW PWM ch 4 | |
| 10 | HW PWM ch 5 | Slice 5, channel A |
| 11 | SW PWM ch 5 | |
| 12 | HW PWM ch 6 | Slice 6, channel A |
| 13 | SW PWM ch 6 | |
| 14 | HW PWM ch 7 | Slice 7, channel A |
| 15 | SW PWM ch 7 | |
| 16 | I2C0 SDA | External pull-up required |
| 17 | I2C0 SCL | External pull-up required |
| 18 | SW PWM ch 8 | |
| 19 | SW PWM ch 9 | |
| 20 | SW PWM ch 10 | |
| 21 | SW PWM ch 11 | |
| 22 | SW PWM ch 12 | |
| 25 | SW PWM ch 13 | On-board LED; remove LED or use as GPIO |
| 26 | SW PWM ch 14 | Shared with ADC0 |
| 27 | SW PWM ch 15 | Shared with ADC1 |
| 28 | — | ADC2; not used by this firmware |
| 29 | — | ADC3; not used by this firmware |

## Reserved Pins

The following pins are **not available** on standard Pico boards because they are used internally:

| GPIO | Usage |
|------|-------|
| 23 | Flash QSPI clock / CS |
| 24 | Flash QSPI data |

Do not use these pins for PWM or general I/O.

## USB Pinout

| Pin | Function |
|-----|----------|
| USB_DP (GPIO 20 alternate) | USB D+ |
| USB_DM (GPIO 19 alternate) | USB D- |
| VBUS | 5 V from USB |
| VSYS | 2..5 V input to on-board regulator |
| 3V3_EN | Regulator enable |
| 3V3_OUT | Regulated 3.3 V output |
| GND | Ground |

The USB CDC interface is used by default. Connect the Pico to a host via USB and it will enumerate as a CDC serial device.

## Electrical Considerations

### Output Drive

- RP2040 GPIOs are 3.3 V logic level.
- Each GPIO can source or sink up to **12 mA** maximum, but staying under **4 mA** is recommended for reliability.
- Driving LEDs, logic gates, MOSFET gates, or optocouplers directly is fine.
- For heavier loads (motors, relays, high-power LEDs), use a buffer, driver, or transistor.

### I2C Pull-ups

GPIO 16 (SDA) and GPIO 17 (SCL) require external pull-up resistors to 3.3 V. Typical values:

- 4.7 kΩ at 100 kHz
- 2.2 kΩ at 400 kHz

Without pull-ups, the I2C bus will not work.

### Shared ADC Pins

GPIO 26, 27, 28, and 29 are connected to ADC inputs through small capacitors on the Pico board. They can still be used as digital outputs, but:

- High-frequency edges may couple slightly into the ADC.
- If you need ADC accuracy, avoid toggling these pins rapidly.

### On-board LED (GPIO 25)

GPIO 25 drives the on-board LED through a 510 Ω resistor. If you use it as a PWM output:

- The LED will light up according to the PWM duty cycle.
- The maximum current is limited by the resistor, so it is safe for the LED.

## Wiring Example

### Basic Test Setup

- Pico powered from USB.
- GPIO 0 → oscilloscope probe / LED with 330 Ω resistor to GND.
- GPIO 16 → SDA of host MCU (with 4.7 kΩ pull-up to 3.3 V).
- GPIO 17 → SCL of host MCU (with 4.7 kΩ pull-up to 3.3 V).
- Host MCU I2C address: `0x40`.

### Host PC Connection

- USB cable from Pico to PC.
- Identify the COM port / tty device (e.g., `COM3` on Windows, `/dev/ttyACM0` on Linux).
- Open a serial terminal at 115200 baud, 8-N-1.

## Safety

- Do not exceed 3.3 V on GPIO pins.
- Do not short VBUS to 3.3 V or GPIO pins.
- Add series resistors when driving capacitive loads or long cables to limit current.
- Always power down before connecting/disconnecting heavy loads.
