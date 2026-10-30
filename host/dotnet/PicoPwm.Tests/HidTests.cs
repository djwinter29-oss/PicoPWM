// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Ji Dong

using System.Buffers.Binary;
using PicoPwm.Host;
using System.Text.Json;

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
        public byte[] IdentityReport = Identity;
        public bool Closed;
        public bool InvalidPins;
        public bool Unplugged;
        public uint[] FreqHz = Enumerable.Repeat(1000u, Protocol.ChannelCount).ToArray();
        public byte[] Duty = Enumerable.Repeat((byte)50, Protocol.ChannelCount).ToArray();

        private byte[] BankReport(int bank)
        {
            var payload = new byte[Protocol.BankStatusSize];
            payload[0] = 1;
            for (var index = 0; index < Protocol.BankSize; index++)
            {
                var offset = 1 + index * 5;
                BinaryPrimitives.WriteUInt32LittleEndian(payload.AsSpan(offset), FreqHz[bank * Protocol.BankSize + index]);
                payload[offset + 4] = Duty[bank * Protocol.BankSize + index];
            }
            return payload;
        }

        public byte[] Get(byte reportId, int size)
        {
            if (Unplugged)
                throw new IOException("device unplugged");
            if (reportId is >= Protocol.BankStatusReport and < Protocol.BankStatusReport + Protocol.BankCount)
                return BankReport(reportId - Protocol.BankStatusReport);
            return reportId switch
            {
                Protocol.IdentityReport => IdentityReport,
                Protocol.ConfigurationReport => Configuration,
                Protocol.MemoryReport => [1, 0xC0, 0xD4, 1, 0, 0, 0x20, 8, 0],
                Protocol.RuntimeReport => [1, 123, 0, 0, 0, 0x74, 0x59, 0, 0, 0x92, 4, 0x40, 0x1A],
                Protocol.PinsReport => InvalidPins ? new byte[28] :
                    [1, .. Enumerable.Range(0, 24).Select(pin => (byte)pin), 26, 27, 255],
                Protocol.ActionReport => Result,
                _ => Bank,
            };
        }

        public void Set(byte reportId, byte[] payload)
        {
            Sent.Add(payload);
            if (mode == "drop")
                return;
            var status = mode == "pending" ? (byte)6
                : mode == "fail-bank" && payload[2] == 3 ? (byte)2
                : (byte)0;
            if (payload[2] == Protocol.OpSetChannel && mode is "invalid-channel" or "invalid-applied")
            {
                status = 2;
                if (mode == "invalid-applied")
                {
                    FreqHz[payload[3]] = BinaryPrimitives.ReadUInt32LittleEndian(payload.AsSpan(4));
                    Duty[payload[3]] = payload[8];
                }
            }
            Result = [1, payload[1], payload[2], status, 0, 0, 0, 0, 0, 0, 0, 0];
        }

        public void Dispose() { Closed = true; }
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
        var rebootIdentity = (byte[])Identity.Clone();
        rebootIdentity[16] = 127;
        Assert.True(Protocol.DecodeIdentity(rebootIdentity).CanSave);
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
    public void InvalidResultIsReconciledWhenReadbackMatches()
    {
        var state = Make("invalid-applied").Item2.SetChannel(0, 2000, 25);
        Assert.Equal(2000u, state.Channels[0].FreqHz);
        Assert.Equal(25, state.Channels[0].Duty);
    }

    [Fact]
    public void InvalidResultRemainsAnErrorWhenReadbackDoesNotMatch()
    {
        var error = Assert.Throws<DeviceException>(() => Make("invalid-channel").Item2.SetChannel(0, 2000, 25));
        Assert.Contains("requested 2000 Hz / 25%", error.Message);
        Assert.Contains("readback 1000 Hz / 50%", error.Message);
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

    private static (FakeDevice, PicoPwmHid) Modern(string mode = "ok")
    {
        var (device, client) = Make(mode);
        device.IdentityReport = (byte[])Identity.Clone();
        device.IdentityReport[16] = 255;
        return (device, client);
    }

    [Fact]
    public void ReadsTelemetryAndGatesLegacyFirmware()
    {
        var status = Modern().Item2.Status();
        Assert.Equal("Pico 2", status.Identity?.BoardModel);
        Assert.Equal(532480u, status.Identity?.RamTotalBytes);
        Assert.Equal(new Protocol.Runtime(123, 22.9, 11.7, 67.2), status.Runtime);
        Assert.True(status.Configuration?.LedOn);
        Assert.True(status.Identity?.CanReadPins);
        Assert.Equal(23, status.Pins?.Channels[23]);
        Assert.Equal(26, status.Pins?.I2cSda);
        Assert.Null(status.Pins?.Led);
        Assert.Null(Make().Item2.Status().Pins);
        Assert.Throws<InvalidDataException>(() => Protocol.DecodePins(new byte[27]));
        Assert.Throws<InvalidDataException>(() => Protocol.DecodePins(
            [1, 48, .. Enumerable.Repeat((byte)0, 26)]));
        Assert.Null(Make().Item2.Status().Runtime);
        Assert.Throws<DeviceException>(() => Make().Item2.SetLed(true));
        Assert.Throws<InvalidDataException>(() => Protocol.DecodeMemory([1, 100, 0, 0, 0, 99, 0, 0, 0]));
        Assert.Throws<InvalidDataException>(() =>
            Protocol.DecodeRuntime([1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x11, 0x27]));
        Assert.Null(Protocol.DecodeRuntime([1, 1, 0, 0, 0, 0, 0, 0, 128, 0, 0, 0, 0]).TemperatureC);
    }

    [Fact]
    public void DashboardJsonMatchesTheSharedBrowserContract()
    {
        using var json = JsonDocument.Parse(JsonSerializer.Serialize(Modern().Item2.Status(),
            new JsonSerializerOptions { PropertyNamingPolicy = JsonNamingPolicy.SnakeCaseLower }));
        var root = json.RootElement;
        Assert.True(root.GetProperty("identity").GetProperty("can_save_reboot").GetBoolean());
        Assert.Equal("Pico 2", root.GetProperty("identity").GetProperty("board_model").GetString());
        Assert.Equal(532480, root.GetProperty("identity").GetProperty("ram_total_bytes").GetInt32());
        Assert.Equal(22.9, root.GetProperty("runtime").GetProperty("temperature_c").GetDouble());
        Assert.Equal(11.7, root.GetProperty("runtime").GetProperty("core0_load").GetDouble());
        Assert.True(root.GetProperty("configuration").GetProperty("led_on").GetBoolean());
        Assert.Equal(26, root.GetProperty("pins").GetProperty("i2c_sda").GetInt32());
        Assert.Equal(32, root.GetProperty("configuration").GetProperty("target").GetProperty("i2c_address").GetInt32());
    }

    [Fact]
    public void PinErrorsAreExplicitAndDisconnectClearsTheMap()
    {
        var (device, client) = Modern();
        device.InvalidPins = true;
        var state = client.Status();
        Assert.True(state.Connected);
        Assert.Null(state.Pins);
        Assert.Contains("pin assignment report", state.PinsError);
        device.InvalidPins = false;
        Assert.Equal(23, client.Status().Pins?.Channels[23]);
        device.Unplugged = true;
        state = client.Status();
        Assert.False(state.Connected);
        Assert.Null(state.Pins);
    }

    [Fact]
    public void SaveAndRebootValidatesBeforeWritesAndOrdersCommands()
    {
        var (device, client) = Modern();
        Assert.Throws<ArgumentException>(() => client.SaveAndReboot(2, 64, [0, 1, 1], false));
        Assert.Throws<ArgumentException>(() => client.SaveAndReboot(0, 120, [0, 1, 1], false));
        Assert.Throws<ArgumentException>(() => client.SaveAndReboot(0, 64, [0, 1], false));
        Assert.Empty(device.Sent);
        client.SaveAndReboot(2, 64, [0, 1, 1], true);
        Assert.Equal(new byte[] { 6, 4, 3, 3, 3, 7 }, device.Sent.Select(data => data[2]).ToArray());
        Assert.Equal(3, device.Sent[^1][9]);
        Assert.True(device.Closed);
    }

    [Fact]
    public void SaveStopsAtFirstFailureAndRebootNeverSaves()
    {
        var (device, client) = Modern("fail-bank");
        Assert.Contains("target settings may have changed",
            Assert.Throws<DeviceException>(() => client.SaveAndReboot(0, 64, [0, 1, 1], false)).Message);
        Assert.Equal(new byte[] { 6, 4, 3 }, device.Sent.Select(data => data[2]).ToArray());
        (device, client) = Modern();
        client.Reboot();
        Assert.Single(device.Sent);
        Assert.Equal(9, device.Sent[0][2]);
        Assert.Equal(Protocol.FlagConfirmSave, device.Sent[0][9]);
    }

    [Fact]
    public void BoardRegistryPinsClientsAndPreservesIdentityAcrossReconnects()
    {
        BoardInfo[] discovered = [BoardInfo.Create("PicoPWM", "A", "/dev/a"),
            BoardInfo.Create("PicoPWM", "B", "/dev/b")];
        using var registry = new BoardRegistry(() => discovered, _ => Modern().Item2, serial: "B");
        var scan = registry.Boards();
        Assert.Equal(discovered[1].Id, scan.DefaultBoardId);
        var a = registry.Client(discovered[0].Id);
        Assert.NotSame(a, registry.Client(discovered[1].Id));
        Assert.Throws<DeviceException>(() => registry.Client("unknown"));
        var id = discovered[0].Id;
        discovered[0] = BoardInfo.Create("PicoPWM", "A", "/dev/new");
        Assert.Equal(id, registry.Boards().Boards[0].Id);
        Assert.Same(a, registry.Client(id));
        discovered = [];
        Assert.Empty(registry.Boards().Boards);
        Assert.Same(a, registry.Client(id));
        discovered = [BoardInfo.Create("PicoPWM", "same", "/a"), BoardInfo.Create("PicoPWM", "same", "/b")];
        Assert.Throws<DeviceException>(() => registry.Boards());
    }
}
