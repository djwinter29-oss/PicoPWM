# CDC/HID Overview

PicoPWM exposes one composite USB device with two host-facing interface types:

- one CDC ACM serial port for the interactive text shell
- one vendor HID interface for identity, configuration, per-bank channel status, and narrow management actions

Use CDC for interactive control. Use HID for browser-based WebHID tools and other HID-capable clients that need
structured reads. I2C remains the binary controller interface. HID is not a second control model: it reuses the
shared channel IDs, capability checks, and result codes.

## Interface Roles

| Interface | Count | Purpose                                              | Host tool                          |
| --------- | ----: | ---------------------------------------------------- | ---------------------------------- |
| CDC ACM   |     1 | Text shell: channel set, stop, status, configuration | Serial terminal                    |
| HID       |     1 | Identity, configuration, channel status, actions     | WebHID, Python, and .NET host apps |
| I2C       |     - | Binary register control from an external controller  | I2C controller                     |

## Architectural Boundaries

The Core 0 and Core 1 split, the `device_api` facade, and the `pwm_driver` multicore boundary are defined in the
[architecture](../architecture.md). HID adds these rules on top:

- HID is another Core 0 transport. It never calls generator or monitor backends and never bypasses
  `pwmdriver/pwm_driver.*`.
- Reads report the realized `pwmdriver` snapshot through `device_api`. HID keeps no shadow channel-state cache.
- USB callbacks stay short and bounded. A set-report callback only copies the request into a single pending slot;
  Core 0 polling validates and executes it. A callback never persists configuration or waits for Core 1.
- HID leaves the CDC interface behavior, the I2C register map, and channel semantics unchanged.

## CDC Behavior

CDC carries the text shell only; its line behavior is unchanged by HID. Commands follow the
[CLI reference](../../guides/control/README.md).

## HID Behavior

HID carries no shell traffic. It reports device identity, the running and target configuration, and the realized
state of every channel in three per-bank reports, and it accepts a small set of actions. Configuration changes take
effect only after save and a restart performed by the user. Board LED and reboot control are out of scope. The
[HID Report Reference](hid-report-reference.md) lists the actions and their rules.

HID does not publish `pulse_count`, which is not a measured count on every backend. HID readback is a snapshot of
frequency and duty, not waveform history; there are no input reports or periodic telemetry, so hosts poll when they need fresh state.

The HID interface is added to the composite USB configuration alongside CDC. Descriptor and report changes are
tested on RP2040 and RP2350 builds and with the target host APIs, including WebHID report-ID handling. Core 0
services HID from its normal polling loop alongside CDC and I2C, and validates framing before calling `device_api`.

## Typical Host Workflow

Read the identity report and confirm the product. Read the configuration once, then poll the bank status reports for
channel state. Submit an action, read the action result until it is no longer pending, and verify the result by reading
the status or configuration report; the result alone is not proof that the realized state changed.
