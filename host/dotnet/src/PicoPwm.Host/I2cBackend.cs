using System.Runtime.InteropServices;
using System.Buffers.Binary;

namespace PicoPwm.Host;

public sealed class I2cBackend : IPicoPwmBackend
{
    private const uint I2cSlave = 0x0703;
    private const int ReadWrite = 2;
    private const byte RegChannels = 0x02;
    private const byte RegGetBase = 0x10;
    private const byte RegSetBase = 0x30;
    private const byte RegStopAll = 0x90;
    private const byte StatusOk = 0;
    private const byte StatusBusy = 1;

    private readonly int _fileDescriptor;
    private readonly int _address;
    private readonly int _pollDelayMilliseconds;

    public I2cBackend(int bus = 1, int address = 0x40, int pollDelayMilliseconds = 20)
    {
        if (address is < 0x08 or > 0x77)
            throw new ArgumentOutOfRangeException(nameof(address), "I2C address must be between 0x08 and 0x77");

        _address = address;
        _pollDelayMilliseconds = pollDelayMilliseconds;
        _fileDescriptor = Native.open($"/dev/i2c-{bus}", ReadWrite);
        if (_fileDescriptor < 0)
            ThrowNativeError($"Unable to open I2C bus {bus}");
        if (Native.ioctl(_fileDescriptor, I2cSlave, address) < 0)
        {
            Native.close(_fileDescriptor);
            ThrowNativeError($"Unable to select I2C address 0x{address:X2}");
        }
    }

    public int ChannelCount => ReadRegister(RegChannels, 1)[0];

    public ChannelState GetChannel(int channel)
    {
        var data = ReadRegister((byte)(RegGetBase + channel), 9);
        if (data.Length == 1)
            throw new InvalidOperationException($"Channel {channel} unavailable (status {data[0]})");
        return new ChannelState(channel, BinaryPrimitives.ReadUInt32LittleEndian(data.AsSpan(0, 4)), data[4],
            BinaryPrimitives.ReadUInt32LittleEndian(data.AsSpan(5, 4)));
    }

    public ChannelState SetChannel(int channel, uint frequencyHz, byte dutyPercent)
    {
        var register = (byte)(RegSetBase + channel);
        var payload = new byte[6];
        payload[0] = register;
        BinaryPrimitives.WriteUInt32LittleEndian(payload.AsSpan(1, 4), frequencyHz);
        payload[5] = dutyPercent;
        WriteAll(payload);
        WaitForStatus(register);
        return GetChannel(channel);
    }

    public void StopAll()
    {
        WriteAll([RegStopAll]);
        WaitForStatus(RegStopAll);
    }

    public void Dispose()
    {
        if (_fileDescriptor >= 0)
            Native.close(_fileDescriptor);
    }

    private byte[] ReadRegister(byte register, int length)
    {
        WriteAll([register]);
        var response = new byte[length];
        ReadAll(response);
        return response;
    }

    private void WaitForStatus(byte register)
    {
        var deadline = DateTime.UtcNow.AddSeconds(2);
        while (true)
        {
            var status = ReadRegister(register, 1)[0];
            if (status != StatusBusy)
            {
                if (status != StatusOk)
                    throw new InvalidOperationException($"I2C command failed with status {status}");
                return;
            }
            if (DateTime.UtcNow >= deadline)
                throw new TimeoutException("Timed out waiting for PicoPWM I2C command");
            Thread.Sleep(_pollDelayMilliseconds);
        }
    }

    private void WriteAll(byte[] data)
    {
        var written = Native.write(_fileDescriptor, data, (nuint)data.Length);
        if (written != data.Length)
            ThrowNativeError("I2C write failed");
    }

    private void ReadAll(byte[] data)
    {
        var read = Native.read(_fileDescriptor, data, (nuint)data.Length);
        if (read != data.Length)
            ThrowNativeError("I2C read failed");
    }

    private static void ThrowNativeError(string message) =>
        throw new IOException($"{message}: {Marshal.GetLastWin32Error()}");

    private static class Native
    {
        [DllImport("libc", SetLastError = true, CharSet = CharSet.Ansi)]
        public static extern int open(string path, int flags);

        [DllImport("libc", SetLastError = true)]
        public static extern int close(int fileDescriptor);

        [DllImport("libc", SetLastError = true)]
        public static extern int ioctl(int fileDescriptor, uint request, int address);

        [DllImport("libc", SetLastError = true)]
        public static extern nint write(int fileDescriptor, byte[] buffer, nuint count);

        [DllImport("libc", SetLastError = true)]
        public static extern nint read(int fileDescriptor, byte[] buffer, nuint count);
    }
}
