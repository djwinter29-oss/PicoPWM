# PicoPWM WebHID Control

**Control and monitor 24 PWM channels directly from your browser.** The page reads PicoPWM identity, configuration,
and realized channel state over a vendor HID interface, and sends channel, bank, I2C address, clock profile, and save
commands. It follows the
[HID report reference](../../docs/design/usb/hid-report-reference.md); there is no application backend.

## Requirements

- Desktop Google Chrome or Microsoft Edge with WebHID enabled; Firefox, Safari, and most mobile browsers are not
  supported
- A Raspberry Pi Pico or Pico 2 running PicoPWM firmware that implements the HID interface (`0xCAFE:0x4012`, HID
  `product_id` `0x0003`), connected to the computer running the browser
- Browser permission and OS access to the board's HID interface; Linux needs a udev rule for the device

## Quick Start

1. Connect the board over a USB data cable and open the page over HTTPS or `localhost`.
2. Click **Connect** and choose the PicoPWM vendor HID interface.
3. Read identity, bank roles, and per-channel frequency and duty. Status refreshes every second.
4. Click a generator's frequency or duty in **PWM control and monitoring** to edit
   both values in a channel dialog, then click **Apply**. Monitor values are read-only.
   Edit boot settings in **Boot configuration**.
5. Click **Save and Reboot**, review the settings in the confirmation dialog, and confirm.
   The page writes the target settings, saves once, and the board reboots. Reconnect to verify the applied settings.

Boot configuration groups controls in order: clock profile, I2C address, hardware
PWM role (channels 0-7), PIO PWM role (channels 8-15), and software PWM role (channels 16-23).
Each bank's backend is fixed; select only Monitor or Generator. Controls initialize
from the device target on connection without overwriting in-progress edits during
status polling. Edits stay local until confirmation; Cancel writes nothing.
I2C input is decimal (8-119); the confirmation also shows hexadecimal.
Firmware must advertise save-and-reboot capability; older firmware shows an update
notice and cannot use the button.

Use one controlling host per board. The page never reopens a device automatically.

**PWM control and monitoring** shows three side-by-side, eight-row tables:
Hardware PWM (channels 0-7), PIO PWM (8-15), and Software PWM (16-23).
Each title shows the running role, not the unsaved boot selection. Columns show
the logical channel index, GPIO number (not physical header pin), frequency, and duty.
Tables stack on narrow screens.
Generator values have a dotted underline and are keyboard-accessible buttons.
The dialog identifies the backend, logical channel, and GPIO, and starts from the
latest realized output (not necessarily the originally requested frequency).
Apply changes the live output immediately and refreshes readback; Cancel writes
nothing. Disconnected devices and channels with failed readings cannot be edited.
An editing hint identifies the interactive generator readings. Boot controls mark
local changes with an amber border and **Unsaved edits**. A separate **Target differs
from running** notice appears for target bank roles or I2C addresses that differ
from the live configuration; it does not claim that the target is saved. The
protocol does not report the running clock profile, so that notice does not infer
clock-profile differences from measured clock speed.

Expand **Pin assignment** below the boot settings to see the fixed channel-to-GPIO
mapping, I2C signals, and board LED pin for Pico and Pico 2. It is collapsed by
default and available without connecting a device. Numbers are GPIO IDs, not
physical header pin numbers.

**Board LED** beside Connect toggles the identification LED and shows its readback
state. **Reboot board** beside Save and Reboot opens a confirmation dialog and
restarts without saving any edits. Both controls require advertised firmware
capabilities and remain disabled on older firmware. LED state is refreshed with
configuration readback once per second, including changes made through CDC.

Serve the page locally with `tools/host/run-webhid.sh` (default `http://127.0.0.1:8000/`, `--port` to change).

## Behavior Notes

- **Runtime status**, between boot settings and channels, follows PicoUart:
  uptime, board temperature, and Core 0 / Core 1 service load, refreshed every
  second. Flash current firmware to enable these readings.
  Temperature is an uncalibrated internal die estimate using a nominal 3.3 V ADC
  reference, not ambient temperature. Service load is service-pass wall time
  divided by its measurement window, excluding idle waits; it is not CPU
  utilization and does not include IRQ work outside measured service passes.
- **Device identity** matches PicoUart's field order: Product, Board model,
  Serial / UID, System clock, Firmware RAM reserved / total, and Firmware version.
  RAM reservation is static SRAM plus reserved core stacks, not live memory
  utilization. RAM readback requires firmware with memory-report capability;
  older firmware shows an explicit update notice instead of an estimated value.
- Frequency `0` means a static level, not a missing reading. Monitors keep their last values for about one second
  after the input stops.
- A save erases flash, so HID, CDC, and I2C all pause briefly and software generators are disturbed. Stop software
  generators first. If the result is unconfirmed, disconnect, reconnect, and check the target settings in report 2 before retrying.
- Clock profiles above 150 MHz are overclocks and require an explicit acknowledgement; only profiles the firmware
  advertises are offered. A save is not a stability qualification. Recover from a profile that prevents boot with
  BOOTSEL.
- Missing or invalid reports show `--` with an explicit read error; no alternate layouts are supported.
- Settings are written as separate target commands before the final save. If any command
  fails, the page stops without sending the save, reports the error, and asks you to
  reconnect and verify potentially partial target changes before retrying.

## Test

```bash
node --test host/webhid/tests/*.test.mjs
```

## Troubleshooting

- **Save and Reboot disabled:** hover over Save and Reboot to read the explanation.
  If the connected firmware lacks the capability, flash the latest firmware for
  your board, then disconnect and reconnect the page. Building firmware alone does
  not update the connected board.
- **Connect disabled:** use desktop Chrome/Edge in a secure context and check whether browser policy disables WebHID.
- **Board not found:** check the firmware, the USB data cable, and browser site permissions.
- **Access denied:** browser permission does not replace OS permissions; close competing HID clients.
- **Remote workspace:** Chrome lists USB devices of the computer running Chrome; a forwarded page does not forward
  USB devices.

## Documentation

- [Project, firmware, and wiring](../../README.md)
- [HID report reference](../../docs/design/usb/hid-report-reference.md)
- [CDC and HID overview](../../docs/design/usb/cdc-hid-overview.md)
