# PIO PWM Backend

The PIO backend uses one state machine per channel, so its capacity is limited
by the MCU's finite PIO resources and fixed pin allocation. The physical map is
documented in the [hardware setup guide](../../guides/hardware-setup.md).

## Generation

A short PIO instruction loop creates continuous PWM without per-edge CPU
service. A timing search selects a period and divider; both frequency and duty
are quantized by the available instruction cycles.

The recommended generation range is **1 Hz to 100 kHz**. Duty resolution
falls quickly as periods shorten, and the reported duty model can diverge
from the physical signal at high frequencies. Treat readback as an applied
timing estimate. Zero-frequency and endpoint-duty requests use static output
levels; see [bank design](banks.md) for accepted request limits.

## Monitoring

The monitor captures one complete high/low period at a time. This keeps
resource use bounded, but readings can lag rapid changes.

Capture time depends on the input period. Short pulses, input synchronization,
and fixed capture overhead limit useful measurement at high frequencies.
The recommended measurement range is **10 Hz to 100 kHz**. Confirm the
intended input range against an instrument.

Generation and monitoring use the same finite PIO resources. The configured
bank role selects which implementation owns them at startup.
