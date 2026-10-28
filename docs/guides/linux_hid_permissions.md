# Linux HID Permissions

The Python and .NET hosts need access to the PicoPWM USB/HID device
(`CAFE:4012`). Run these commands once from the account that will use the host:

```sh
tools/host/hid-permissions-setup.sh
tools/host/hid-permissions-check.sh
```

The setup helper uses sudo to install a udev rule for PicoPWM USB and hidraw
nodes, granting access to the account's primary group and desktop `uaccess`. It
does not change group membership or overwrite a customized rule. The Python
and .NET launchers check permissions before starting and print setup guidance
when access is denied. Reconnect the board after setup if it remains
inaccessible. WebHID permissions are managed by the browser on the computer
connected to the board.
