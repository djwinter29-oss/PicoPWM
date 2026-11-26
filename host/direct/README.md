# PicoPWM Direct Dashboard

Control 24 PWM channels over browser USB HID. No host service is needed.

[Open Direct Dashboard](https://djwinter29-oss.github.io/PicoPWM/) ·
[Firmware](https://github.com/djwinter29-oss/PicoPWM/releases) ·
[Wiring](../../docs/guides/hardware-setup.md) · [HID reference](../../docs/protocols/usb-hid.md)

## Requirements

- Desktop Chrome or Edge, using HTTPS or a loopback address. Firefox, Safari, and mobile browsers are not supported.
- A Pico or Pico 2 running compatible PicoPWM firmware, connected to the browser's computer by a USB data cable.
- Browser device permission and OS access; Linux needs [HID permissions setup](../../docs/guides/linux-hid-access.md).

Firmware compatibility follows HID report layouts and advertised capabilities,
not a minimum version. No minimum hardware-verified firmware release has been
established; unsupported identities or layouts produce protocol errors.

## Quick Start

1. Connect the board, open the dashboard, and click **Connect**.
2. Select the PicoPWM HID interface and grant permission.
3. Check device identity and running bank roles. Banks default to monitor-only.
4. If the bank is a monitor, first
   [stop and verify existing generators](../../docs/guides/dashboard-controls.md#stop-an-output).
   Select Generator under **Boot configuration**, confirm **Save & Reboot**,
   reconnect, and verify the role.
5. Click a generator's underlined frequency or duty, edit the values, and click **Apply**.
   **This changes live output immediately; verify the GPIO and wiring first.**

Use one controlling host per board. Status refreshes every second; reconnect manually.
Disconnecting does **not** stop outputs; use the
[stop procedure](../../docs/guides/dashboard-controls.md#stop-an-output).

## Controls and Readings

See [channel controls, readings, and saving](../../docs/guides/dashboard-controls.md).

## Troubleshooting

- **Cannot connect:** check the browser, secure context, USB data cable, firmware, and device permissions.
  Close other HID clients. USB access is local to the browser's computer, not the remote workspace.
- **Command timed out:** disconnect and reconnect before retrying. WebHID cannot cancel an outstanding
  transfer, and a timed-out write may have taken effect.
- **Save & Reboot disabled:** check the button's explanation. There may be no pending changes, or firmware may
  lack the capability. Flashing firmware updates the board; building alone does not.

## Development

[Local builds and tests](../../docs/development/host-apps.md#direct-dashboard).
