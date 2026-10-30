# Security

## Report a Vulnerability

Please report suspected vulnerabilities privately through the repository's **Security** tab using **Report a
vulnerability**. Do not publish exploit details in an issue or pull request.

If private reporting is unavailable, open a minimal issue asking maintainers for a private reporting channel; do not
include technical details until one is provided.

## Device Security Notes

- The USB identity `0xCAFE:0x4012` is an unallocated development/lab identity. Do not use it for commercial
  products; collisions and driver-association conflicts are possible.
- The USB CDC shell, USB HID interface and I2C slave have no authentication. Any local process that can open the CDC
  or HID device, and any device on the I2C bus, can change PWM outputs and the startup configuration. Restrict
  operating-system access to the device nodes to trusted users and keep the I2C bus private.
- Saving a configuration writes flash and can select an overclocked system clock. A saved profile that prevents boot
  is recovered by holding BOOTSEL while connecting USB and reflashing.
- The host dashboards are local tools, not network services. They bind to loopback unless
  `PICOPWM_TOKEN` is set. HTTP error bodies use fixed sentences; the server log keeps the
  underlying detail. `GET /api/boards` includes the local `device_path` so a dashboard can
  reopen the HID node, and an authenticated client on a non-loopback bind can read that path.
  Do not expose the dashboards on untrusted networks.
