# Linux HID Access

Run this setup on the Linux computer connected to the board. The scripts
come from the source repository, not the Python package or .NET archive.
If you need a checkout:

```sh
git clone https://github.com/djwinter29-oss/PicoPWM.git
cd PicoPWM
tools/host/hid-permissions-setup.sh
tools/host/hid-permissions-check.sh
```

For later checks, use the existing checkout.

The helper uses `sudo` to install a USB/`hidraw` udev rule for your primary
group and desktop `uaccess`. It neither changes group membership nor
overwrites customized rules. Reconnect the board if access is still denied,
then rerun the check.

Direct Dashboard also needs browser permission. Rules on a remote server do
not grant access to USB devices attached to your browser's computer.

## Upgrading from the lab USB identity

The allocated identity `0x2E8A:0x116C` replaces `0xCAFE:0x4012`. Update firmware
and hosts together: current clients do not discover the lab identity.
Grant browser permission again; the CDC device name may also change.

In `/etc/udev/rules.d/70-picopwm-hid.rules`, replace `cafe`/`4012` with
`2e8a`/`116c`, preserving local customizations. If the file differs from the
helper's default, do not rerun the setup helper: it refuses to overwrite
customized rules. After reviewing the rule, reload udev and check access:

```sh
sudo udevadm control --reload-rules
sudo udevadm trigger --subsystem-match=usb --attr-match=idVendor=2e8a --attr-match=idProduct=116c
sudo udevadm trigger --subsystem-match=hidraw
sudo udevadm settle
```

Reconnect the board, then run `tools/host/hid-permissions-check.sh` from the
repository checkout.
