# HIL Test Records

Store completed hardware test reports and supporting captures here.

| Record | Scope |
| --- | --- |
| [Manual Pico bank-rotation loopback](2026-10-30-manual-pico-bank-rotation.md) | Operator-reported loopback, each bank as generator, 100-1000 Hz, 25-75% duty |

Name records `YYYY-MM-DD-<description>.md`, for example
`2026-10-27-pico2-pwm-loopback.md`, so they sort by date. Reference case IDs
from the [HIL test plan](../test-plan.md), including failed, skipped, and
blocked cases.

Include:

- Date, board model, firmware version, commit, and artifact SHA-256.
- Host OS, application/version, control interface, wiring, instruments, sample
  rate, and measurement settings.
- Running bank roles, system clock, I2C address, and test conditions.
- Requested, realized, and independently measured values with acceptance limits.
- Pass/fail results, skipped checks, logs, and capture filenames.

Do not include credentials or unrelated personal information in logs and captures.

A generated runner report covers only its automated subset, not the full HIL plan.
