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
    public static IFeatureDevice Open(string? serial = null, string? path = null)
    {
        var devices = DeviceList.Local.GetHidDevices(Protocol.VendorId, Protocol.ProductId)
            .Where(device => device.GetReportDescriptor().DeviceItems
                .Any(item => item.Usages.GetAllValues().Contains(Protocol.UsagePageAndUsage)))
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

    public HidSharpDevice(HidDevice device)
    {
        this.device = device;
        stream = device.Open();
        // ponytail: HidSharp timeouts bound reads and writes; PicoUart additionally runs each transfer on a
        // watchdog thread, which is only needed if a driver is seen to ignore them.
        stream.ReadTimeout = stream.WriteTimeout = 3000;
    }

    public byte[] Get(byte reportId, int size)
    {
        // HidSharp on Linux echoes the report ID twice, so the payload starts at offset 2 there.
        var prefix = OperatingSystem.IsLinux() ? 2 : 1;
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
