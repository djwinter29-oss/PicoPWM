# Control Interfaces

PicoPWM exposes one logical control model through transport-specific
interfaces. Both interfaces use the same channel IDs, realized state, device
identity, and board-control operations.

## Shared Semantics

- `get` and `status` report realized channel state.
- `set` applies only to output-capable channels in the configured banks.
- Monitor-only or disabled channels report an unavailable operation.
- `stop` uses configured generator banks' safe reset behavior; monitor banks do
  not modify measured input state.
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
