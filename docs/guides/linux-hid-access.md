# Linux HID Access

The Python and .NET host applications need operating-system access to the
PicoPWM HID device. Run these commands once from the Linux account that will
use the host:

```sh
tools/host/hid-permissions-setup.sh
tools/host/hid-permissions-check.sh
```

The setup helper uses `sudo` to install a udev rule for PicoPWM USB and
`hidraw` nodes. It grants access to the account's primary group and desktop
`uaccess`; it does not change group membership or overwrite a customized
rule. Reconnect the board after setup if it remains inaccessible.

Direct Dashboard also requires browser permission to access the board. Run the udev setup on the computer
physically connected to the board; permission on a remote server does not
grant the browser access to a local USB device.

## Upgrading from the lab USB identity

PicoPWM now uses the Raspberry Pi-allocated USB identity `0x2E8A:0x116C`,
replacing `0xCAFE:0x4012`. Update firmware and host applications together;
current HID clients do not discover firmware using the old lab identity.
Grant browser permission again for the new identity. The OS may also assign
a different CDC serial device name.

On Linux, review `/etc/udev/rules.d/70-picopwm-hid.rules` and replace the old
`cafe`/`4012` vendor/product matches with `2e8a`/`116c`, preserving any local
customizations. Then rerun the setup helper above to reload the rules.
The helper intentionally refuses to overwrite a differing existing rule.
