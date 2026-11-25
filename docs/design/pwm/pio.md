# PIO PWM Backend

The PIO backend uses one state machine per channel, so its capacity is limited
by the MCU's finite PIO resources and fixed pin allocation. The physical map is
documented in the [hardware setup guide](../../guides/hardware-setup.md).

## Generation

A short PIO instruction loop creates continuous PWM without per-edge CPU
service. A timing search selects a period and divider; both frequency and duty
are quantized by the available instruction cycles.

The recommended generation range is **1 Hz to 100 kHz**. Higher requests are
experimental, up to the **10 MHz firmware request limit**. Duty resolution
falls quickly as periods shorten, and the reported duty model can diverge
from the physical signal at high frequencies. Treat readback as an applied
timing estimate, not an instrument result. Zero-frequency and endpoint-duty
requests use static output levels.

## Monitoring

The monitor captures one complete high/low period at a time and publishes
the latest sample. This keeps resource use bounded, but readings can lag rapid
changes and do not provide waveform history.

Capture time depends on the input period. Short pulses, input synchronization,
and fixed capture overhead limit useful measurement at high frequencies.
The recommended measurement range is **10 Hz to 100 kHz**, not a qualified
accuracy envelope. There is no qualified absolute maximum for this monitor;
the generator's request limit does not define monitor accuracy. Confirm the
intended input range against an instrument.

Generation and monitoring use the same finite PIO resources. The configured
bank role selects which implementation owns them at startup.
