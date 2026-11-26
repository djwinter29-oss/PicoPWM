# Troubleshooting

Commands below run in the [USB CDC shell](../protocols/usb-cdc.md).

- [Serial device missing](#the-board-does-not-appear-in-the-terminal)
- [HID access denied](#a-python-or-net-host-reports-hid-access-denied)
- [Browser cannot connect](#direct-dashboard-cannot-find-or-open-the-board)
- [Unexpected output](#pwm-output-stays-low-or-does-not-match-the-request)
- [Configuration not applied](#a-configuration-change-does-not-take-effect)
- [Flash recovery](#recover-an-unbootable-configuration-or-reset-flash)
- [Unexpected monitor reading](#a-monitor-reports-no-signal-or-an-unexpected-reading)

## The board does not appear in the terminal

- Use a USB data cable and check board power.
- Confirm PicoPWM firmware is running and open its CDC device: `/dev/ttyACM*` on Linux,
  `COMx` on Windows, and `/dev/cu.usbmodem*` on macOS.
- Close other programs using the port. USB CDC ignores the baud-rate setting.
- Reconnect the board and retry. If necessary, reflash the appropriate
  `pico` or `pico2` firmware from [Releases](https://github.com/djwinter29-oss/PicoPWM/releases).

## A Python or .NET host reports HID access denied

- On Linux, run [HID setup and its check](linux-hid-access.md), reconnect,
  and check again.
- Close another program that may have the HID device open.
- Connect the board to the computer running the host application.

## Direct Dashboard cannot find or open the board

- Use desktop Chrome or Edge.
- Connect the board to the browser's computer and grant site access when prompted.
- On Linux, also configure OS access as described in
  [Linux HID Access](linux-hid-access.md). Browser permission does not replace
  device-node permission.
- Close other HID clients and retry the browser connection.

## PWM output stays low or does not match the request

- Run `config get` and check the bank's **running** role. If it is a monitor,
  first run `pwm stop all`, then `pwm get all`. Confirm every generator reports
  `0 Hz / 0%` and is disabled. If stop fails or the state is uncertain,
  disconnect board power and do not save. Otherwise, use only the matching
  command:

  | Channels | Command |
  | --- | --- |
  | 0-7 | `config set banka=generator` |
  | 8-15 | `config set bankb=generator` |
  | 16-23 | `config set bankc=generator` |

  It saves and reboots. Reconnect and verify the role before driving output.
- Check the channel-to-GPIO mapping in [Hardware Setup](hardware-setup.md)
  before connecting an instrument or load.
- Compare `pwm get <channel>` with the request. `0 Hz / 0%` holds a generator
  low; `0 Hz / 100%` holds it high.
- Confirm signal grounds are shared and that connected signals are 3.3 V
  safe. Do not connect outputs together.
- Verify the pin with an oscilloscope or logic analyzer.
  Request limits are not accuracy guarantees; see the
  [operating ranges](configuration.md#pwm-operating-ranges).

## A configuration change does not take effect

- After reconnecting, compare `config get` with the request.
  It reports running settings; roles change only after reboot.
- If the configured clock could not be applied, the CDC shell prints a warning
  on connection. Run `board get clock` to check the actual clock in Hz.
- CDC `config set` saves and reboots. HID changes are targets until saved
  and rebooted; confirm **Save & Reboot** in a dashboard.
- Check supported roles, clock profiles, and I2C addresses. CDC rejects an
  invalid assignment set without applying it. After a failed HID sequence,
  reread targets: earlier settings may have been applied.

## Recover an unbootable configuration or reset flash

Use BOOTSEL if a saved clock profile prevents startup. Reflashing firmware
may preserve configuration; a full flash reset clears it.

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

- Check the bank is a monitor and the signal reaches the correct GPIO.
- Check the signal voltage, common ground, and frequency against the selected
  backend's measurement range.
- Allow settling time: hardware takes about half a second, PIO captures a
  complete period, and software can miss narrow pulses. Readings are samples,
  not waveform history. Verify changing signals with an instrument.

For bug reports, include board, firmware, interface, configuration, action,
and result. Check whether it reproduces after reconnecting at the default clock.
