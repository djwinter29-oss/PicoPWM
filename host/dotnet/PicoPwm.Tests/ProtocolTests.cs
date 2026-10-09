using PicoPwm.Host;

namespace PicoPwm.Host.Tests;

public sealed class ProtocolTests
{
    [Fact]
    public void ParsesRealizedChannelResponse()
    {
        var state = CdcResponseParser.ParseChannel(
            "CH7: freq=1200 Hz, duty=35%, pulses=9, enabled=yes");

        Assert.Equal(7, state.Channel);
        Assert.Equal((uint)1200, state.FrequencyHz);
        Assert.Equal((byte)35, state.DutyPercent);
        Assert.Equal((uint)9, state.PulseCount);
        Assert.True(state.Enabled);
    }

    [Fact]
    public void CountsFirmwareStatusRows()
    {
        var rows = new[]
        {
            "=== All PWM channels (logical 0..23) ===",
            "Ch  Backend  State  Freq(Hz)   Duty(%)   Pulses",
            "0   hw       OFF            0      50  0",
            "1   pio      ON          1000      50  4"
        };

        Assert.Equal(2, CdcResponseParser.CountStatusRows(rows));
    }

    [Fact]
    public void RejectsMalformedChannelResponse()
    {
        Assert.Throws<FormatException>(() => CdcResponseParser.ParseChannel("ERR channel unavailable"));
    }

    [Fact]
    public void RejectsInvalidI2cAddressesBeforeOpeningHardware()
    {
        Assert.Throws<ArgumentOutOfRangeException>(() => new I2cBackend(address: 0x07));
        Assert.Throws<ArgumentOutOfRangeException>(() => new I2cBackend(address: 0x78));
    }
}
