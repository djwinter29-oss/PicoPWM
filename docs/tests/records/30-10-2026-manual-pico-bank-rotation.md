# Manual Pico Bank-Rotation Loopback

Operator-reported manual functional check. Every value below comes from the operator's report; fields marked
*not recorded* were not provided. This record does not include captured logs or instrument measurements.

## Identity and Conditions

| Item | Value |
| --- | --- |
| Date | 30-10-2026 |
| Board | Raspberry Pi Pico |
| Firmware version / commit / SHA-256 | Not recorded |
| Control interface | Not recorded |
| System clock | Not recorded |
| Fixture | Operator's HIL setup; wiring details not recorded (reference: [HIL connection guide](../hil-test-connections.md)) |
| Independent instrument | None reported; monitor readback only |

## Procedure

Reported by the operator: each bank was configured as the generator one at a time. For each generator bank, all
eight channels were set to each test point below, and the monitor readback was compared against the requested
values. The order of banks and which monitor channels were read were not recorded.

| Parameter | Values |
| --- | --- |
| Frequencies | 100, 500, 1000 Hz |
| Duty cycles | 25%, 50%, 75% |
| Frequency tolerance | ±5% (minimum 1 Hz) |
| Duty tolerance | ±5 percentage points |

## Results

| Generator | Channels | Result |
| --- | --- | --- |
| Bank A | 8 | PASS: operator reported monitor readback within tolerance |
| Bank B | 8 | PASS: operator reported monitor readback within tolerance |
| Bank C | 8 | PASS: operator reported monitor readback within tolerance |

Overall result: **PASS** for all reported manual test points above.

Numerical readings: not recorded.

## Not Covered

- Independent instrument accuracy (PWM-01, MON-01), jitter and settling-time measurement.
- 0%/100% duty, zero-frequency, boundary and out-of-range cases (PWM-03, PWM-05).
- Pico 2 and overclock profiles.

This record shows functional loopback operation on the tested setup only; it is not accuracy qualification.
