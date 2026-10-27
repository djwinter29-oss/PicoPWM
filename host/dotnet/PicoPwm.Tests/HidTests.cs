using PicoPwm.Host;

namespace PicoPwm.Tests;

public sealed class HidTests
{
    private static readonly byte[] Identity = [1, 3, 0, 1, 2, 3, 4, 5, 6, 7, 8, 1, 2, 3, 2, 24, 3, 0x1F];
    private static readonly byte[] Configuration =
        [1, 0, 1, 2, 0, 0, 1, 0x20, 0, 1, 2, 0, 0, 1, 0x20, 1, 1, 0x80, 0xD1, 0xF0, 0x08];
    private static readonly byte[] Bank = [1, .. Enumerable.Range(0, 8).SelectMany(_ => new byte[] { 0xE8, 0x03, 0, 0, 50 })];

    private sealed class FakeDevice(string mode = "ok") : IFeatureDevice
    {
        public byte[] Result = [1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0];
        public List<byte[]> Sent = [];

        public byte[] Get(byte reportId, int size) => reportId switch
        {
            Protocol.IdentityReport => Identity,
            Protocol.ConfigurationReport => Configuration,
            Protocol.ActionReport => Result,
            _ => Bank,
        };

        public void Set(byte reportId, byte[] payload)
        {
            Sent.Add(payload);
            if (mode != "drop")
                Result = [1, payload[1], payload[2], (byte)(mode == "pending" ? 6 : 0), 0, 0, 0, 0, 0, 0, 0, 0];
        }

        public void Dispose() { }
    }

    private static (FakeDevice, PicoPwmHid) Make(string mode = "ok")
    {
        var device = new FakeDevice(mode);
        long now = 0;
        return (device, new PicoPwmHid(() => device, _ => { }, () => now++));
    }

    [Fact]
    public void DecodesDocumentedReports()
    {
        Assert.Equal("RP2350", Protocol.DecodeIdentity(Identity).Mcu);
        Assert.Equal("0102030405060708", Protocol.DecodeIdentity(Identity).SerialNumber);
        var config = Protocol.DecodeConfiguration(Configuration);
        Assert.Equal(150_000_000u, config.SystemClockHz);
        Assert.Equal(new Protocol.ChannelState(8, 1000, 50), Protocol.DecodeBankStatus(Bank, 1)[0]);
    }

    [Fact]
    public void RejectsBadLayouts()
    {
        Assert.Throws<InvalidDataException>(() => Protocol.DecodeIdentity(Identity[..^1]));
        Assert.Throws<InvalidDataException>(() => Protocol.DecodeActionResult([1, 1, 1, 9, 0, 0, 0, 0, 0, 0, 0, 0]));
    }

    [Fact]
    public void EncodesSetChannel() => Assert.Equal(
        new byte[] { 1, 7, 1, 9, 0xE8, 3, 0, 0, 50, 0, 0, 0 },
        Protocol.EncodeActionRequest(7, Protocol.OpSetChannel, 9, 1000, 50));

    [Fact]
    public void StatusReadsAllBanks()
    {
        var status = Make().Item2.Status();
        Assert.True(status.Connected);
        Assert.Equal(1000u, status.Channels[23].FreqHz);
    }

    [Fact]
    public void StatusWithoutDeviceReportsError()
    {
        var status = new PicoPwmHid(() => throw new InvalidOperationException("PicoPWM HID interface not found")).Status();
        Assert.False(status.Connected);
        Assert.Contains("not found", status.Error);
    }

    [Fact]
    public void SetChannelCompletes()
    {
        var (device, client) = Make();
        client.SetChannel(3, 2000, 25);
        Assert.Equal(new byte[] { 1, 1, 1, 3 }, device.Sent[0][..4]);
    }

    [Fact]
    public void DroppedSetIsReported() =>
        Assert.Contains("no change was made", Assert.Throws<DeviceException>(() => Make("drop").Item2.SetChannel(0, 1, 1)).Message);

    [Fact]
    public void PendingTimesOutAsUnconfirmed() =>
        Assert.Contains("unconfirmed", Assert.Throws<DeviceException>(() => Make("pending").Item2.SetChannel(0, 1, 1)).Message);

    [Fact]
    public void ValidatesInputAndOverclockAcknowledgement()
    {
        var client = Make().Item2;
        Assert.Throws<ArgumentException>(() => client.SetChannel(24, 1, 1));
        Assert.Throws<ArgumentException>(() => client.SetClockProfile(3, false));
        client.SetClockProfile(3, true);
    }
}
