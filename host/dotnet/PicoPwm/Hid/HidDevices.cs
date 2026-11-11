// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Ji Dong

using HidSharp;

namespace PicoPwm.Host;

/// <summary>Numbered HID feature reports; payloads exclude the report ID byte.</summary>
public interface IFeatureDevice : IDisposable
{
    byte[] Get(byte reportId, int size);

    void Set(byte reportId, byte[] payload);
}

public static class HidDevices
{
    private static IEnumerable<HidDevice> MatchingDevices(IEnumerable<HidDevice> devices) => devices
        .Where(device => device.VendorID == Protocol.VendorId && device.ProductID == Protocol.ProductId)
        .Where(device => device.GetReportDescriptor().DeviceItems
            .Any(item => item.Usages.GetAllValues().Contains(Protocol.UsagePageAndUsage)));

    public static BoardInfo[] Enumerate() =>
        Enumerate(DeviceList.Local.GetHidDevices(Protocol.VendorId, Protocol.ProductId));

    internal static BoardInfo[] Enumerate(IEnumerable<HidDevice> devices) => MatchingDevices(devices)
        .Select(device => BoardInfo.Create(device.GetProductName(), device.GetSerialNumber(), device.DevicePath))
        .ToArray();

    public static IFeatureDevice Open(string? serial = null, string? path = null) =>
        Open(DeviceList.Local.GetHidDevices(Protocol.VendorId, Protocol.ProductId), serial, path);

    internal static IFeatureDevice Open(IEnumerable<HidDevice> candidates, string? serial = null, string? path = null)
    {
        var devices = MatchingDevices(candidates)
            .Where(device => serial is null || device.GetSerialNumber() == serial)
            .Where(device => path is null || device.DevicePath == path)
            .ToArray();
        if (devices.Length == 0)
            throw new InvalidOperationException("PicoPWM HID interface not found");
        if (devices.Length > 1)
            throw new InvalidOperationException(
                "multiple PicoPWM HID interfaces found; set PICOPWM_SERIAL or PICOPWM_DEVICE_PATH");
        return new HidSharpDevice(devices[0]);
    }
}

internal sealed class HidSharpDevice : IFeatureDevice
{
    private readonly HidDevice device;
    private readonly HidStream stream;
    private readonly int prefix;

    public HidSharpDevice(HidDevice device) : this(device, OperatingSystem.IsLinux()) { }

    internal HidSharpDevice(HidDevice device, bool linux)
    {
        this.device = device;
        prefix = linux ? 2 : 1;
        stream = device.Open();
        // ponytail: Configure ordinary I/O timeouts; native feature-report stalls remain backend-dependent.
        // Keep synchronous transfers until qualification shows a need for isolated transfer execution.
        stream.ReadTimeout = stream.WriteTimeout = 3000;
    }

    public byte[] Get(byte reportId, int size)
    {
        // HidSharp on Linux echoes the report ID twice, so the payload starts at offset 2 there.
        var report = new byte[Math.Max(size + prefix, device.GetMaxFeatureReportLength() + prefix - 1)];
        report[0] = reportId;
        stream.GetFeature(report);
        if (report[0] != reportId || (prefix == 2 && report[1] != reportId))
            throw new InvalidDataException("unexpected HID feature report ID");
        return report.AsSpan(prefix, size).ToArray();
    }

    public void Set(byte reportId, byte[] payload) => stream.SetFeature([reportId, .. payload]);

    public void Dispose() => stream.Dispose();
}
