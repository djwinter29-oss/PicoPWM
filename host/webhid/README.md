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
4. Use **Set channel** or **Stop all** for generators. Bank, I2C address, and clock profile changes edit the target
   configuration only.
5. Click **Save target**, then restart the board yourself. HID has no reboot command, so the saved target applies only
   after a power cycle or reset. Reconnect and reread the configuration.

Use one controlling host per board. The page never reopens a device automatically.

Serve the page locally with `tools/host/run-webhid.sh` (default `http://127.0.0.1:8000/`, `--port` to change).

## Behavior Notes

- Frequency `0` means a static level, not a missing reading. Monitors keep their last values for about one second
  after the input stops.
- Reports have no pulse counter; use CDC or I2C when an event count is needed.
- A save erases flash, so HID, CDC, and I2C all pause briefly and software generators are disturbed. Stop software
  generators first. If the result is unconfirmed, disconnect, reconnect, and check the target settings in report 2 before retrying.
- Clock profiles above 150 MHz are overclocks and require an explicit acknowledgement; only profiles the firmware
  advertises are offered. A save is not a stability qualification. Recover from a profile that prevents boot with
  BOOTSEL.
- Missing or invalid reports show `--` with an explicit read error; no alternate layouts are supported.

## Test

```bash
node --test host/webhid/tests/*.test.mjs
```

## Troubleshooting

- **Connect disabled:** use desktop Chrome/Edge in a secure context and check whether browser policy disables WebHID.
- **Board not found:** check the firmware, the USB data cable, and browser site permissions.
- **Access denied:** browser permission does not replace OS permissions; close competing HID clients.
- **Remote workspace:** Chrome lists USB devices of the computer running Chrome; a forwarded page does not forward
  USB devices.

## Documentation

- [Project, firmware, and wiring](../../README.md)
- [HID report reference](../../docs/design/usb/hid-report-reference.md)
- [CDC and HID overview](../../docs/design/usb/cdc-hid-overview.md)
