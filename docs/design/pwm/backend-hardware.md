# Hardware PWM Backend

The hardware PWM backend uses the MCU's dedicated PWM slices. It offers the
most precise generation path in PicoPWM, within the limits of the fixed pin
and slice allocation described in the
[hardware setup guide](../../guides/hardware-setup.md).

## Generation

Each channel has an independent slice. The backend chooses a divider and
period count to approximate the requested frequency, then derives the output
duty from the available counter steps. This trades frequency range against
duty resolution: as frequency rises, fewer steps remain in each period.
Readback reports the programmed timing model, not an electrical measurement.

Static endpoints are handled without a periodic waveform: zero frequency or
zero duty produces a low output; full duty produces a high output. The
recommended range is **10 Hz to 1 MHz**. Duty steps become coarse above that
range. The minimum representable periodic frequency also depends on the
running system clock; see [bank design](banks.md) for accepted request limits.

Verify physical frequency, duty, and signal integrity when the application
depends on them.

## Monitoring

The monitor uses the PWM input counters. Because each slice provides one
counter, it measures frequency and high time in separate windows rather than
simultaneously. A complete reading therefore takes about half a second and
can combine information from different moments if the signal changes.

The windowed approach has coarse frequency resolution at the low end and
depends on timely counter servicing at the high end. The project recommends
measurement from **100 Hz to 1 MHz**, but does not claim a qualified absolute
maximum or accuracy across that range. Use another backend for slower signals
and verify measurements against an instrument when accuracy matters.
