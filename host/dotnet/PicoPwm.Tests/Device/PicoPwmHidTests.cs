// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Ji Dong

using PicoPwm.Host;
using Microsoft.Extensions.Logging;
using static PicoPwm.Tests.Fakes;

namespace PicoPwm.Tests;

public sealed class PicoPwmHidTests
{
    [Fact]
    public void StatusPinSnapshotsDoNotExposeCachedArrays()
    {
        var client = Modern().Item2;
        var first = client.Status();
        first.Pins!.Channels[0] = 47;
        Assert.Equal(0, client.Status().Pins!.Channels[0]);
    }

    [Theory]
    [InlineData(false)]
    [InlineData(true)]
    public void PrerequisiteFailuresAreDeviceErrorsWithTheirCause(bool saveReboot)
    {
        var cause = new IOException("unplugged");
        var client = new PicoPwmHid(() => throw cause);
        var error = Assert.Throws<DeviceException>(() =>
        {
            if (saveReboot) client.SaveAndReboot(0, 64, [0, 1, 1], false);
            else client.SetClockProfile(0, false);
        });
        Assert.Same(cause, error.InnerException);
        Assert.Equal(409, DashboardHttp.FromException(error).StatusCode);
        Assert.Null(error.Status);
    }

    [Fact]
    public void InvalidBootDraftIsRejectedBeforeOpeningHardware()
    {
        var opened = false;
        var client = new PicoPwmHid(() =>
        {
            opened = true;
            throw new IOException("unplugged");
        });
        Assert.Throws<ArgumentException>(() => client.SaveAndReboot(0, 64, null!, false));
        Assert.Throws<ArgumentException>(() => client.SaveAndReboot(0, 120, [0, 1, 1], false));
        Assert.Throws<ArgumentException>(() => client.SaveAndReboot(3, 64, [0, 1, 1], false));
        Assert.Throws<ArgumentException>(() => client.SetClockProfile(5, false));
        Assert.False(opened);
    }

    [Fact]
    public void SaveAndRebootUsesAnIndependentRolesDraft()
    {
        var (device, _) = Modern();
        int[] roles = [0, 1, 1];
        var client = new PicoPwmHid(() =>
        {
            roles[0] = 99;
            return device;
        });
        client.SaveAndReboot(0, 64, roles, false);
        Assert.Equal(0, device.Sent[2][4]);
        Assert.Equal(1, device.Sent[3][5]);
        Assert.Equal(1, device.Sent[4][5]);
    }

    [Fact]
    public void FirmwareFailuresPreserveStatusAndWrappedCauses()
    {
        var invalid = Assert.Throws<DeviceException>(() => Make("invalid-channel").Item2.SetChannel(0, 2000, 25));
        Assert.Equal(Protocol.StatusInvalid, invalid.Status);
        Assert.IsType<DeviceException>(invalid.InnerException);
        var save = Assert.Throws<DeviceException>(() =>
            Modern("fail-bank").Item2.SaveAndReboot(0, 64, [0, 1, 1], false));
        Assert.Equal(Protocol.StatusInvalid, save.Status);
        Assert.IsType<DeviceException>(save.InnerException);
        Assert.Contains("target settings may have changed", save.Message);
        var cause = new IOException("unplugged");
        var transfer = Assert.Throws<DeviceException>(() => new PicoPwmHid(() => throw cause).Stop());
        Assert.Same(cause, transfer.InnerException);
        var reboot = Assert.Throws<DeviceException>(() => new PicoPwmHid(() => throw cause).Reboot());
        Assert.Same(cause, reboot.InnerException);
    }

    [Fact]
    public void ReconciliationHoldsTheLockUntilTheWholeOperationFinishes()
    {
        var (device, _) = Make("invalid-channel");
        PicoPwmHid? client = null;
        var checks = 0;
        client = new PicoPwmHid(() => device, _ =>
        {
            AssertGateHeld(client!);
            checks++;
        });
        Assert.Throws<DeviceException>(() => client.SetChannel(0, 2000, 25));
        Assert.Equal(2, checks);
        client.Stop();
        Assert.Equal(new byte[] { Protocol.OpSetChannel, Protocol.OpStop },
            device.Sent.Select(command => command[2]).ToArray());
    }

    [Theory]
    [InlineData(false)]
    [InlineData(true)]
    public void ClockAndSaveHoldTheGateDuringPrerequisiteReadsAndCommandDispatch(bool save)
    {
        var (device, client) = Modern();
        var reads = new List<byte>();
        device.BeforeGet = report =>
        {
            reads.Add(report);
            AssertGateHeld(client);
        };
        device.BeforeSet = _ => AssertGateHeld(client, nested: true);
        if (save) client.SaveConfiguration(false);
        else client.SetClockProfile(0, false);
        Assert.Contains(Protocol.IdentityReport, reads);
        if (save) Assert.Contains(Protocol.ConfigurationReport, reads);
        Assert.Equal(save ? Protocol.OpSaveConfig : Protocol.OpSetClockProfile, Assert.Single(device.Sent)[2]);
    }

    private static void AssertGateHeld(PicoPwmHid client, bool nested = false)
    {
        // Inspect ownership without exposing a production lock hook or relying on a scheduling delay.
        var gate = typeof(PicoPwmHid).GetField("gate",
            System.Reflection.BindingFlags.Instance | System.Reflection.BindingFlags.NonPublic)!.GetValue(client)!;
        Assert.True(Monitor.IsEntered(gate));
        if (nested)
        {
            // Balance Run's inner entry temporarily: the logical operation must still own its outer entry.
            Monitor.Exit(gate);
            try { Assert.True(Monitor.IsEntered(gate), "Prerequisite validation and dispatch need one outer lock"); }
            finally { Monitor.Enter(gate); }
        }
        var acquired = false;
        var contender = new Thread(() =>
        {
            acquired = Monitor.TryEnter(gate);
            if (acquired) Monitor.Exit(gate);
        }) { IsBackground = true };
        contender.Start();
        Assert.True(contender.Join(TimeSpan.FromSeconds(5)), "Lock contender did not execute");
        Assert.False(acquired);
    }

    [Fact]
    public void DisposeIsLazyClosesOnlyAndLogsCleanupFailures()
    {
        var opened = false;
        using (var unused = new PicoPwmHid(() =>
        {
            opened = true;
            throw new IOException("must not open");
        })) { }
        Assert.False(opened);
        var (device, _) = Make();
        var logger = new RecordingLogger();
        var client = new PicoPwmHid(() => device, logger: logger);
        var bodyError = new InvalidOperationException("script failed");
        device.CloseError = new IOException("close failed");
        void RunScript()
        {
            using (client)
            {
                client.Status();
                throw bodyError;
            }
        }
        var caught = Assert.Throws<InvalidOperationException>(RunScript);
        Assert.Same(bodyError, caught);
        Assert.True(device.Closed);
        Assert.Empty(device.Sent);
        Assert.Same(device.CloseError, Assert.Single(logger.Errors));
        client.Dispose();
        Assert.Single(logger.Errors);
    }

    private sealed class RecordingLogger : ILogger<PicoPwmHid>
    {
        public List<Exception?> Errors { get; } = [];
        public IDisposable? BeginScope<TState>(TState state) where TState : notnull => null;
        public bool IsEnabled(LogLevel logLevel) => true;
        public void Log<TState>(LogLevel logLevel, EventId eventId, TState state, Exception? exception,
            Func<TState, Exception?, string> formatter)
        {
            Assert.Equal(LogLevel.Warning, logLevel);
            Errors.Add(exception);
        }
    }

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
