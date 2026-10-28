# Control Interfaces

PicoPWM exposes one logical control model through transport-specific
interfaces. Both interfaces use the same channel IDs, realized state, device
identity, and board-control operations.

## Shared Semantics

- Channel reads report realized state, not just the last requested values.
- Channel updates apply only to output-capable channels in the configured banks.
- Monitor-only or disabled channels report an unavailable operation.
- Configuration changes affect the next boot, not active PWM hardware. On CDC,
  `config set` validates the complete request, saves it, and reboots to apply
  the target. I2C and HID use their own save and reboot operations.
- Channel backend, direction, GPIO, and capabilities come from the fixed Bank
  A/B/C allocation. Hosts may rely on those stable channel ranges.

On the CDC shell, use `pwm get <channel>` to read one channel,
`pwm get all` to read all channels, and
`pwm set <channel> <freq_hz> [duty_percent]` to update a generator. Use
`config get [item]` and `config set <key=value>[, ...]` for startup settings.
See the [USB CDC CLI guide](usb_cdc_cli.md) for all commands and accepted
configuration values.

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
