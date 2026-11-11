// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Ji Dong

using HidSharp;
using PicoPwm.Host;

namespace PicoPwm.Tests;

public sealed class HidDevicesTests
{
    [Fact]
    public void DiscoveryFiltersIdentityAndUsageAndPreservesBoardMetadata()
    {
        var selected = new TestHidDevice();
        HidDevice[] devices = [new TestHidDevice(vendor: 1), new TestHidDevice(product: 1),
            new TestHidDevice(usage: 2), selected];
        var board = Assert.Single(HidDevices.Enumerate(devices));
        Assert.Equal(BoardInfo.Create("PicoPWM test", "test", "/fake/test"), board);
        Assert.Equal(0, selected.Opens);
    }

    [Fact]
    public void OpenRejectsAbsentAmbiguousAndMismatchedBoardsBeforeOpening()
    {
        var a = new TestHidDevice(serial: "A");
        var b = new TestHidDevice(serial: "B");
        Assert.Throws<InvalidOperationException>(() => HidDevices.Open(Array.Empty<HidDevice>()));
        Assert.Throws<InvalidOperationException>(() => HidDevices.Open([a, b]));
        Assert.Throws<InvalidOperationException>(() => HidDevices.Open([a, b], serial: "missing"));
        Assert.Throws<InvalidOperationException>(() => HidDevices.Open([a, b], serial: "A", path: b.DevicePath));
        Assert.Throws<InvalidOperationException>(() => HidDevices.Open([new TestHidDevice(usage: 2)]));
        Assert.Equal(0, a.Opens);
        Assert.Equal(0, b.Opens);
    }

    [Theory]
    [InlineData(true)]
    [InlineData(false)]
    public void OpenSelectsOnlyTheRequestedSerialOrPath(bool bySerial)
    {
        var a = new TestHidDevice(serial: "A");
        var b = new TestHidDevice(serial: "B");
        using (HidDevices.Open([a, b], serial: bySerial ? "B" : null, path: bySerial ? null : b.DevicePath))
        {
            Assert.Equal(0, a.Opens);
            Assert.Equal(1, b.Opens);
            Assert.Equal(3000, b.Stream.ReadTimeout);
            Assert.Equal(3000, b.Stream.WriteTimeout);
        }
        Assert.True(b.Stream.WasDisposed);
    }

    [Theory]
    [InlineData(false, 8)]
    [InlineData(true, 8)]
    [InlineData(false, 64)]
    [InlineData(true, 64)]
    public void GetUsesPlatformPrefixAndReturnsOnlyTheRequestedPayload(bool linux, int maximum)
    {
        var device = new TestHidDevice(maximum: maximum);
        using var adapter = new HidSharpDevice(device, linux);
        var prefix = linux ? 2 : 1;
        device.Stream.FillFeature = report =>
        {
            Assert.Equal(Protocol.IdentityReport, report[0]);
            Assert.Equal(Math.Max(Protocol.IdentitySize + prefix, maximum + prefix - 1), report.Length);
            if (linux) report[1] = report[0];
            Fakes.Identity.CopyTo(report, prefix);
        };
        Assert.Equal(Fakes.Identity, adapter.Get(Protocol.IdentityReport, Protocol.IdentitySize));
    }

    [Theory]
    [InlineData(false, false)]
    [InlineData(true, false)]
    [InlineData(true, true)]
    public void GetRejectsWrongReportIds(bool linux, bool secondId)
    {
        var device = new TestHidDevice();
        using var adapter = new HidSharpDevice(device, linux);
        device.Stream.FillFeature = report =>
        {
            if (linux) report[1] = report[0];
            report[secondId ? 1 : 0] = 99;
        };
        Assert.Throws<InvalidDataException>(() => adapter.Get(Protocol.IdentityReport, Protocol.IdentitySize));
    }

    [Fact]
    public void SetPrefixesReportIdAndTransportFailuresPropagate()
    {
        var device = new TestHidDevice();
        using var adapter = new HidSharpDevice(device);
        byte[] payload = [1, 2, 3];
        adapter.Set(Protocol.ActionReport, payload);
        Assert.Equal(new byte[] { Protocol.ActionReport, 1, 2, 3 }, device.Stream.Written);
        Assert.Equal(new byte[] { 1, 2, 3 }, payload);
        var cause = new IOException("USB disconnected");
        device.Stream.Failure = cause;
        Assert.Same(cause, Assert.Throws<IOException>(() => adapter.Get(Protocol.IdentityReport, Protocol.IdentitySize)));
        Assert.Same(cause, Assert.Throws<IOException>(() => adapter.Set(Protocol.ActionReport, payload)));
    }

    [Fact]
    public void OpenFailuresPropagateWithoutTryingAnotherBoard()
    {
        var device = new TestHidDevice();
        var cause = new UnauthorizedAccessException("HID permission denied");
        device.OpenError = cause;
        Assert.Same(cause, Assert.Throws<UnauthorizedAccessException>(() => HidDevices.Open([device])));
        Assert.Equal(1, device.Opens);
    }

    private sealed class TestHidDevice(int vendor = Protocol.VendorId, int product = Protocol.ProductId,
        byte usage = 1, string serial = "test", int maximum = 64) : HidDevice
    {
        public int Opens { get; private set; }
        public Exception? OpenError { get; set; }
        public TestHidStream Stream { get; private set; } = null!;
        public override int VendorID => vendor;
        public override int ProductID => product;
        public override int ReleaseNumberBcd => 0x100;
        public override string DevicePath => $"/fake/{serial}";
        public override int GetMaxInputReportLength() => maximum;
        public override int GetMaxOutputReportLength() => maximum;
        public override int GetMaxFeatureReportLength() => maximum;
        public override string GetFileSystemName() => DevicePath;
        public override string GetProductName(GetStringFlags flags) => "PicoPWM test";
        public override string GetSerialNumber(GetStringFlags flags) => serial;
        public override byte[] GetRawReportDescriptor() =>
            [0x06, 0x00, 0xFF, 0x09, usage, 0xA1, 0x01, 0x85, 0x01,
             0x15, 0x00, 0x26, 0xFF, 0x00, 0x75, 0x08, 0x95, 0x12, 0xB1, 0x02, 0xC0];
        protected override DeviceStream OpenDeviceDirectly(OpenConfiguration configuration)
        {
            Opens++;
            if (OpenError is not null) throw OpenError;
            return Stream = new TestHidStream(this);
        }
    }

    private sealed class TestHidStream(HidDevice device) : HidStream(device)
    {
        public Action<byte[]>? FillFeature { get; set; }
        public Exception? Failure { get; set; }
        public byte[]? Written { get; private set; }
        public bool WasDisposed { get; private set; }
        public override int ReadTimeout { get; set; }
        public override int WriteTimeout { get; set; }
        public override void GetFeature(byte[] buffer, int offset, int count)
        {
            if (Failure is not null) throw Failure;
            Assert.Equal(0, offset);
            Assert.Equal(buffer.Length, count);
            FillFeature?.Invoke(buffer);
        }
        public override void SetFeature(byte[] buffer, int offset, int count)
        {
            if (Failure is not null) throw Failure;
            Written = buffer.AsSpan(offset, count).ToArray();
        }
        public override int Read(byte[] buffer, int offset, int count) => throw new NotSupportedException();
        public override void Write(byte[] buffer, int offset, int count) => throw new NotSupportedException();
        protected override void Dispose(bool disposing)
        {
            WasDisposed = true;
            base.Dispose(disposing);
        }
    }
}
