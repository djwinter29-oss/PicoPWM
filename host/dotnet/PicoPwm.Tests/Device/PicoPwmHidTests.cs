// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Ji Dong

using PicoPwm.Host;
using static PicoPwm.Tests.Fakes;

namespace PicoPwm.Tests;

public sealed class PicoPwmHidTests
{
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
}
