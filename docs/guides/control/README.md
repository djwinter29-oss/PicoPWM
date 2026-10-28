# Control Interfaces

PicoPWM exposes one logical control model through transport-specific
interfaces. Both interfaces use the same channel IDs, realized state, device
identity, and board-control operations.

## Shared Semantics

- `get` and `status` report realized channel state.
- `set` applies only to output-capable channels in the configured banks.
- Monitor-only or disabled channels report an unavailable operation.
- `config` changes only the next-boot target; it never changes active PWM
  hardware until the board reboots.
- Channel backend, direction, GPIO, and capabilities come from the fixed Bank
  A/B/C allocation. Hosts may rely on those stable channel ranges.

## Interfaces

- [USB CDC CLI](usb_cdc_cli.md) for interactive text commands and scripts.
- [I2C Protocol](i2c_protocol.md) for the binary register interface.
- [CDC/HID Overview](../../design/usb/cdc-hid-overview.md) and
  [HID Report Reference](../../design/usb/hid-report-reference.md) for the
  WebHID-compatible management interface.

See [Firmware Configuration](../configuration.md) for bank behavior and
[Architecture](../../design/architecture.md) for the shared control path.

## Linux HID Permissions

The Python and .NET hosts need access to the PicoPWM USB/HID device (`CAFE:4012`).
Run these commands once from the account that will use the host:

```sh
tools/host/hid-permissions-setup.sh
tools/host/hid-permissions-check.sh
```

The setup helper uses sudo to install a udev rule for PicoPWM USB and hidraw nodes,
granting access to the account's primary group and desktop `uaccess`. It does not
change group membership or overwrite a customized rule. The Python and .NET launchers
check permissions before starting and print setup guidance when access is denied.
Reconnect the board after setup if it remains inaccessible. WebHID permissions are
managed by the browser on the computer connected to the board.
