using System.Buffers.Binary;

namespace PicoPwm.Host;

/// <summary>HID feature-report layouts (version 1); see docs/design/usb/hid-report-reference.md.</summary>
public static class Protocol
{
    public const ushort VendorId = 0xCAFE;
    public const ushort ProductId = 0x4012;
    public const uint UsagePageAndUsage = 0xFF000001;
    public const ushort HidProductId = 3;

    public const byte IdentityReport = 1, ConfigurationReport = 2, BankStatusReport = 10, ActionReport = 20;
    public const byte MemoryReport = 3, RuntimeReport = 4;
    public const int MemorySize = 9, RuntimeSize = 13;
    public const int ChannelCount = 24, BankCount = 3, BankSize = 8;
    public const int IdentitySize = 18, ConfigurationSize = 21, BankStatusSize = 41, ActionSize = 12;

    public const byte OpSetChannel = 1, OpStop = 2, OpSetBank = 3, OpSetI2cAddress = 4, OpSaveConfig = 5,
        OpSetClockProfile = 6, OpSaveReboot = 7, OpSetLed = 8, OpReboot = 9;
    public const byte FlagConfirmSave = 1, FlagAckOverclock = 2;
    public const byte StatusOk = 0, StatusPending = 6;

    private static readonly string[] StatusNames =
        ["OK", "BUSY", "INVALID", "UNAVAILABLE", "TIMEOUT", "APPLY_FAILED", "PENDING", "UNSUPPORTED", "STORAGE_FAILED"];

    public sealed record Identity(int ProductId, string SerialNumber, string FirmwareVersion, string Mcu,
        int ChannelCount, bool CanSave, bool CanSetClock, int ClockProfileMask,
        bool CanSaveReboot, bool CanSetLed, bool CanReboot, bool CanReadMemory, bool CanReadRuntime,
        string BoardModel, uint? RamReservedBytes = null, uint? RamTotalBytes = null);

    public sealed record BankConfiguration(int[] Backends, int[] Roles, int I2cAddress);

    public sealed record Configuration(BankConfiguration Running, BankConfiguration Target,
        int ClockProfile, uint SystemClockHz, bool LedOn);

    public sealed record Memory(uint RamReservedBytes, uint RamTotalBytes);
    public sealed record Runtime(uint UptimeSeconds, double? TemperatureC, double Core0Load, double Core1Load);

    public sealed record ChannelState(int Channel, uint? FreqHz, int? Duty);

    public sealed record ActionResult(int TransactionId, int Operation, int Status, string StatusName);

    private static void Check(byte[] payload, int size, string name)
    {
        if (payload.Length != size)
            throw new InvalidDataException($"{name} must contain exactly {size} payload bytes (received {payload.Length})");
        if (payload[0] != 1)
            throw new InvalidDataException($"unsupported {name} layout version {payload[0]}");
    }

    public static Identity DecodeIdentity(byte[] payload)
    {
        Check(payload, IdentitySize, "identity report");
        var product = BinaryPrimitives.ReadUInt16LittleEndian(payload.AsSpan(1));
        if (product != HidProductId)
            throw new InvalidDataException($"unsupported product ID 0x{product:x4}");
        int mcu = payload[14], channels = payload[15], capabilities = payload[16], profiles = payload[17];
        if (mcu is not (1 or 2) || channels != ChannelCount || (capabilities & ~0x7F) != 0 || (profiles & ~0x1F) != 0
            || ((capabilities & 2) != 0) != (profiles != 0) || (profiles != 0 && (profiles & 2) == 0))
            throw new InvalidDataException("invalid identity capabilities");
        return new(product, Convert.ToHexString(payload.AsSpan(3, 8)), $"{payload[11]}.{payload[12]}.{payload[13]}",
            mcu == 1 ? "RP2040" : "RP2350", channels, (capabilities & 1) != 0, (capabilities & 2) != 0, profiles,
            (capabilities & 4) != 0, (capabilities & 8) != 0, (capabilities & 16) != 0,
            (capabilities & 32) != 0, (capabilities & 64) != 0, mcu == 1 ? "Pico" : "Pico 2");
    }

    private static BankConfiguration DecodeBanks(byte[] payload, int offset)
    {
        var backends = payload.AsSpan(offset, 3).ToArray().Select(value => (int)value).ToArray();
        var roles = payload.AsSpan(offset + 3, 3).ToArray().Select(value => (int)value).ToArray();
        if (backends.Any(value => value > 2) || roles.Any(value => value > 1) || payload[offset + 6] > 127)
            throw new InvalidDataException("invalid bank configuration");
        return new(backends, roles, payload[offset + 6]);
    }

    public static Configuration DecodeConfiguration(byte[] payload)
    {
        Check(payload, ConfigurationSize, "configuration report");
        var clock = BinaryPrimitives.ReadUInt32LittleEndian(payload.AsSpan(17));
        if (payload[16] > 4 || clock == 0)
            throw new InvalidDataException("invalid configuration fields");
        return new(DecodeBanks(payload, 1), DecodeBanks(payload, 8), payload[16], clock, (payload[15] & 1) != 0);
    }

    public static Memory DecodeMemory(byte[] payload)
    {
        Check(payload, MemorySize, "memory report");
        var reserved = BinaryPrimitives.ReadUInt32LittleEndian(payload.AsSpan(1));
        var total = BinaryPrimitives.ReadUInt32LittleEndian(payload.AsSpan(5));
        if (total == 0 || reserved > total)
            throw new InvalidDataException("invalid memory report");
        return new(reserved, total);
    }

    public static Runtime DecodeRuntime(byte[] payload)
    {
        Check(payload, RuntimeSize, "runtime report");
        var temperature = BinaryPrimitives.ReadInt32LittleEndian(payload.AsSpan(5));
        var core0 = BinaryPrimitives.ReadUInt16LittleEndian(payload.AsSpan(9));
        var core1 = BinaryPrimitives.ReadUInt16LittleEndian(payload.AsSpan(11));
        if (core0 > 10000 || core1 > 10000)
            throw new InvalidDataException("invalid runtime service load");
        return new(BinaryPrimitives.ReadUInt32LittleEndian(payload.AsSpan(1)),
            temperature == int.MinValue ? null : temperature / 1000.0, core0 / 100.0, core1 / 100.0);
    }

    public static ChannelState[] DecodeBankStatus(byte[] payload, int bank)
    {
        Check(payload, BankStatusSize, $"bank status report {BankStatusReport + bank}");
        var channels = new ChannelState[BankSize];
        for (var index = 0; index < BankSize; index++)
        {
            var duty = payload[5 + index * 5];
            if (duty > 100)
                throw new InvalidDataException("invalid duty in bank status");
            channels[index] = new(bank * BankSize + index,
                BinaryPrimitives.ReadUInt32LittleEndian(payload.AsSpan(1 + index * 5)), duty);
        }
        return channels;
    }

    public static ActionResult DecodeActionResult(byte[] payload)
    {
        Check(payload, ActionSize, "action result report");
        if (payload[3] >= StatusNames.Length || payload.AsSpan(4).ToArray().Any(value => value != 0))
            throw new InvalidDataException("invalid action result");
        return new(payload[1], payload[2], payload[3], StatusNames[payload[3]]);
    }

    public static byte[] EncodeActionRequest(byte transactionId, byte operation, byte channel = 0, uint arg0 = 0,
        byte arg1 = 0, byte flags = 0)
    {
        var request = new byte[ActionSize];
        request[0] = 1;
        request[1] = transactionId;
        request[2] = operation;
        request[3] = channel;
        BinaryPrimitives.WriteUInt32LittleEndian(request.AsSpan(4), arg0);
        request[8] = arg1;
        request[9] = flags;
        return request;
    }
}
