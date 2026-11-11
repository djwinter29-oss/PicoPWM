# PicoPWM Direct Dashboard

Control and monitor 24 PWM channels directly from your browser over USB HID. No Python or .NET host service is needed.

[Open Direct Dashboard](https://djwinter29-oss.github.io/PicoPWM/) ·
[Firmware and wiring](../../README.md) · [HID reference](../../docs/design/usb/hid-report-reference.md)

## Requirements

- Desktop Chrome or Edge, using HTTPS or a loopback address. Firefox, Safari, and mobile browsers are not supported.
- A Pico or Pico 2 running compatible PicoPWM firmware, connected to the browser's computer by a USB data cable.
- Browser device permission and OS access; Linux needs [HID permissions setup](../../docs/guides/setup-and-troubleshooting.md#linux-hid-access).

## Quick Start

1. Connect the board, open the dashboard, and click **Connect**.
2. Select the PicoPWM HID interface and grant permission.
3. Check device identity and running bank roles. Banks default to monitor-only.
4. If the intended bank is currently a monitor, select Generator under **Boot configuration**, then review and
   confirm **Save & Reboot**. Reconnect and verify the applied settings. Skip this step if it is already a generator.
5. Click a generator's underlined frequency or duty, edit the values, and click **Apply**.
   **This changes live output immediately; verify the GPIO and wiring first.**

Use one controlling host per board. Status refreshes every second; reconnection is manual.

## Controls and Readings

| Control | Behavior |
| --- | --- |
| PWM tables | Hardware channels 0-7, PIO 8-15, software 16-23. Backends are fixed; roles apply to whole banks. Monitors are read-only. |
| Channel editor | Starts from realized output. Hz/kHz/MHz conversion preserves frequency. The 1 kHz / 50% preset only fills fields; Cancel writes nothing. |
| Boot configuration | Clock profile, decimal I2C address (8-119), and bank roles. Edits remain local until confirmed. |
| Pin assignment | Expand for GPIO IDs, not physical header-pin numbers. |
| Board LED / Reboot Board | Toggle the LED or confirm a restart without saving edits. |

**Unsaved edits** marks local changes. **Target differs from running** does not prove settings are saved or
identify clock-profile differences. Missing readings show explicit errors; unsupported features require newer firmware.

Readback is not an independent electrical measurement. RAM is static reservation, temperature is an uncalibrated
internal estimate, and service load is not CPU utilization. See the [HID reference](../../docs/design/usb/hid-report-reference.md)
for interpretation and the [frequency guidance](../../README.md#recommended-frequency-ranges) for output limitations.

## Saving Safely

- Stop software generators before saving: flash writes briefly pause HID, CDC, and I2C and disturb software output.
- Profiles above 150 MHz require overclock acknowledgement; 150 MHz also overclocks RP2040.
  Saving does not establish stability. For an unbootable profile, follow the
  [BOOTSEL and flash-reset recovery guide](../../docs/guides/setup-and-troubleshooting.md#recover-an-unbootable-configuration-or-reset-flash).
- A failed settings command prevents the final save but may leave partial target changes.
  After a failed or unconfirmed operation, reconnect and inspect target settings before retrying.

## Troubleshooting

- **Cannot connect:** check the browser, secure context, USB data cable, firmware, and device permissions.
  Close competing HID clients. USB access is local to the browser's computer, even with a remote workspace.
- **Save & Reboot disabled:** check the button's explanation. There may be no pending changes, or firmware may
  lack the capability. Flashing firmware updates the board; building alone does not.

## Development

From the repository root, serve locally and run tests in separate terminals:

```sh
tools/host/run-direct.sh
node --test host/direct/tests/*.test.mjs
```

The launcher needs Python 3, with no extra packages, and serves `http://127.0.0.1:8000/` (`--port` to override).
Only `src/` is served or deployed; it also supplies shared assets to Python/.NET hosts.
`tests/` contains hardware-free checks, not physical PWM qualification.
