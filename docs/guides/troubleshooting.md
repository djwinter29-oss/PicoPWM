# Troubleshooting

Find the next check when the board, a host application, or an output does not
behave as expected. Commands below use the [USB CDC shell](usb-cdc-shell.md).

## The board does not appear in the terminal

- Confirm the board is powered and use a USB data cable, not a charge-only
  cable.
- Check that the board is running PicoPWM firmware and that the host opened
  its CDC serial device. Common device names are `/dev/ttyACM*` on Linux,
  `COMx` on Windows, and `/dev/cu.usbmodem*` on macOS.
- Close another terminal or program that may already have the serial port
  open. USB CDC does not depend on the terminal baud-rate setting.
- Reconnect the board and retry. If necessary, reflash the appropriate
  `pico` or `pico2` firmware from [Releases](https://github.com/djwinter29-oss/PicoPWM/releases).

## A Python or .NET host reports HID access denied

- On Linux, run the two commands in [Linux HID Access](linux-hid-access.md),
  reconnect the board, and run the permission check again.
- Close another program that may have the HID device open.
- Confirm the board is connected to the same computer that runs the host
  application.

## Direct Dashboard cannot find or open the board

- Use desktop Chrome or Edge, the supported browsers for Direct Dashboard.
- Connect the board directly to the computer running the browser, then grant
  the site access when prompted.
- On Linux, also configure OS access as described in
  [Linux HID Access](linux-hid-access.md). Browser permission does not replace
  device-node permission.
- Close other HID clients and retry the browser connection.

## PWM output stays low or does not match the request

- Run `config get` and check the **running** role for the channel's bank. If
  it is a monitor, configure that bank as a generator and apply the change
  before requesting output.
- Check the channel-to-GPIO mapping in [Hardware Setup](hardware-setup.md)
  before connecting an instrument or load.
- Run `pwm get <channel>` and compare the realized frequency and duty with
  the request. A generator at `0 Hz / 0%` is intentionally held low; at
  `0 Hz / 100%` it is held high.
- Confirm signal grounds are shared and that connected signals are 3.3 V
  safe. Do not connect outputs together.
- Use a suitable oscilloscope or logic analyzer to verify the physical
  output. Requested frequency ranges are not accuracy guarantees; see the
  [backend design pages](../design/pwm/).

## A configuration change does not take effect

- After reconnecting, compare the actual values from `config get` with your
  request. It reports running settings only; bank roles change only after reboot.
- If the configured clock could not be applied, the CDC shell prints a warning
  on connection. Run `board get clock` to check the actual clock in Hz.
- CDC `config set` saves and reboots automatically. HID configuration changes
  have their own save and reboot workflow; consult the
  [HID Report Reference](../protocols/usb/hid-report-reference.md).
- If a setting is rejected, check that the bank role, clock profile, or I2C
  address is supported. A rejected request is not partially applied.

## Recover an unbootable configuration or reset flash

If a saved clock profile prevents normal startup, use BOOTSEL to recover the board.
Reinstalling firmware alone may preserve saved configuration; a full flash reset clears it.

**A full flash reset erases all firmware, saved configuration, and other data on the board.**
Disconnect power and attached loads before recovery, and reconnect only USB for the steps below.

1. Download Raspberry Pi's official
   [universal flash-nuke UF2](https://github.com/raspberrypi/pico-sdk-prebuilts/releases/latest/download/nuke_universal.uf2),
   which supports both Pico and Pico 2. See the official
   [resetting flash memory instructions](https://www.raspberrypi.com/documentation/microcontrollers/pico-series.html#resetting-flash-memory).
2. Hold **BOOTSEL** while connecting the board with a USB data cable, then release it when the boot drive appears.
3. Copy the flash-nuke UF2 to that drive and wait for erasure to finish. The board returns to BOOTSEL mode.
4. Copy the correct board-specific PicoPWM UF2 from
   [Releases](https://github.com/djwinter29-oss/PicoPWM/releases) to the boot drive.
5. Reconnect through the CDC shell or a dashboard and verify the firmware version and running configuration.
   Reconfigure bank roles, I2C address, and clock settings as needed before reconnecting loads.

## A monitor reports no signal or an unexpected reading

- Confirm the bank is configured as a monitor and the signal is connected to
  the GPIO for the logical channel.
- Check the signal voltage, common ground, and frequency against the selected
  backend's measurement range.
- Monitor readings are sampled values, not waveform history. Rapidly changing
  signals may change between samples; see the relevant
  [backend design](../design/pwm/).

For unresolved problems, record the board model, firmware version, interface,
configuration, command or host action, and observed result. Include whether the
issue reproduces after reconnecting with the default clock profile.
