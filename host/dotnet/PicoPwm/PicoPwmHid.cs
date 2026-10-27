using static PicoPwm.Host.Protocol;

namespace PicoPwm.Host;

/// <summary>The device rejected a command, or its outcome could not be confirmed.</summary>
public sealed class DeviceException(string message) : Exception(message);

public sealed record DashboardStatus(bool Connected, Identity? Identity, string? IdentityError,
    Configuration? Configuration, string? ConfigurationError, ChannelState[] Channels, string?[] BankErrors,
    string? Error, Protocol.Runtime? Runtime = null, string? RuntimeError = null,
    Pins? Pins = null, string? PinsError = null);

/// <summary>Thread-safe client for one board; opens lazily and reopens after an I/O failure.</summary>
public sealed class PicoPwmHid(Func<IFeatureDevice> opener, Action<TimeSpan>? sleep = null, Func<long>? clockMs = null)
{
    private static readonly TimeSpan ActionTimeout = TimeSpan.FromSeconds(3), SaveTimeout = TimeSpan.FromSeconds(15);
    private readonly object gate = new();
    private readonly Action<TimeSpan> sleep = sleep ?? Thread.Sleep;
    private readonly Func<long> clockMs = clockMs ?? (() => Environment.TickCount64);
    private IFeatureDevice? device;
    private Identity? identity;
    private Configuration? configuration;
    private Pins? pins;
    private byte transactionId;

    public void Close()
    {
        lock (gate)
        {
            var closing = device;
            device = null;
            identity = null;
            configuration = null;
            pins = null;
            try { closing?.Dispose(); } catch (IOException) { }
        }
    }

    private IFeatureDevice Device => device ??= opener();

    private byte[] Read(byte report, int size) => Device.Get(report, size);

    private Identity EnsureIdentity()
    {
        if (identity is not null) return identity;
        var current = DecodeIdentity(Read(IdentityReport, IdentitySize));
        if (current.CanReadMemory)
        {
            var memory = DecodeMemory(Read(MemoryReport, MemorySize));
            current = current with { RamReservedBytes = memory.RamReservedBytes, RamTotalBytes = memory.RamTotalBytes };
        }
        return identity = current;
    }

    public DashboardStatus Status()
    {
        lock (gate)
        {
            var channels = Enumerable.Range(0, ChannelCount).Select(channel => new ChannelState(channel, null, null))
                .ToArray();
            var bankErrors = new string?[BankCount];
            string? configurationError = null;
            Protocol.Runtime? runtime = null;
            string? runtimeError = null;
            string? pinsError = null;
            try
            {
                var current = EnsureIdentity();
                if (current.CanReadPins)
                {
                    try { pins ??= DecodePins(Read(PinsReport, PinsSize)); }
                    catch (InvalidDataException error) { pinsError = error.Message; }
                }
                configuration = null;
                try { configuration = DecodeConfiguration(Read(ConfigurationReport, ConfigurationSize)); }
                catch (InvalidDataException error) { configurationError = error.Message; }
                if (current.CanReadRuntime)
                {
                    try { runtime = DecodeRuntime(Read(RuntimeReport, RuntimeSize)); }
                    catch (InvalidDataException error) { runtimeError = error.Message; }
                }
                for (var bank = 0; bank < BankCount; bank++)
                {
                    try
                    {
                        DecodeBankStatus(Read((byte)(BankStatusReport + bank), BankStatusSize), bank)
                            .CopyTo(channels, bank * BankSize);
                    }
                    catch (InvalidDataException error) { bankErrors[bank] = error.Message; }
                }
                return new(true, current, null, configuration, configurationError, channels, bankErrors, null,
                    runtime, runtimeError, pins, pinsError);
            }
            catch (InvalidDataException error)
            {
                Close();
                return new(false, null, error.Message, null, null, channels, bankErrors, error.Message);
            }
            catch (Exception error) when (error is IOException or InvalidOperationException or UnauthorizedAccessException
                or NotSupportedException or TimeoutException)
            {
                Close();
                return new(false, null, null, null, null, channels, bankErrors, error.Message);
            }
        }
    }

    private void Perform(byte operation, byte channel = 0, uint arg0 = 0, byte arg1 = 0, byte flags = 0)
    {
        var current = EnsureIdentity();
        if ((operation == OpSaveConfig && !current.CanSave) || (operation == OpSetClockProfile && !current.CanSetClock)
            || (operation == OpSaveReboot && !current.CanSaveReboot) || (operation == OpSetLed && !current.CanSetLed)
            || (operation == OpReboot && !current.CanReboot))
            throw new DeviceException("the firmware does not advertise this capability");
        if (DecodeActionResult(Read(ActionReport, ActionSize)).Status == StatusPending)
            throw new DeviceException("a previous command is still pending");
        transactionId = (byte)(transactionId % 255 + 1);
        Device.Set(ActionReport, EncodeActionRequest(transactionId, operation, channel, arg0, arg1, flags));
        var deadline = clockMs() + (long)(operation is OpSaveConfig or OpSaveReboot ? SaveTimeout : ActionTimeout).TotalMilliseconds;
        for (var first = true; ; first = false)
        {
            var result = DecodeActionResult(Read(ActionReport, ActionSize));
            if (result.TransactionId != transactionId || result.Operation != operation)
                throw new DeviceException(first
                    ? "the device did not accept the request; no change was made"
                    : "the result no longer matches the request; outcome is unconfirmed");
            if (result.Status != StatusPending)
            {
                if (result.Status != StatusOk)
                    throw new DeviceException($"command failed: {result.StatusName}");
                return;
            }
            if (clockMs() >= deadline)
                throw new DeviceException("timed out awaiting the result; outcome is unconfirmed");
            sleep(TimeSpan.FromMilliseconds(50));
        }
    }

    private DashboardStatus Run(bool changesConfiguration, byte operation, byte channel = 0, uint arg0 = 0,
        byte arg1 = 0, byte flags = 0)
    {
        lock (gate)
        {
            try
            {
                Perform(operation, channel, arg0, arg1, flags);
            }
            catch (DeviceException)
            {
                throw;
            }
            catch (Exception error) when (error is IOException or InvalidOperationException or InvalidDataException
                or UnauthorizedAccessException or NotSupportedException or TimeoutException)
            {
                Close();
                throw new DeviceException($"HID transfer failed; outcome is unconfirmed: {error.Message}");
            }
            if (changesConfiguration)
                configuration = null;
            return Status();
        }
    }

    public DashboardStatus SetChannel(int channel, long freqHz, int duty)
    {
        if (channel is < 0 or >= ChannelCount)
            throw new ArgumentException($"channel must be between 0 and {ChannelCount - 1}");
        if (freqHz is < 0 or > uint.MaxValue)
            throw new ArgumentException("freq_hz must be an unsigned 32-bit integer");
        if (duty is < 0 or > 100)
            throw new ArgumentException("duty must be between 0 and 100");
        return Run(false, OpSetChannel, (byte)channel, (uint)freqHz, (byte)duty);
    }

    public DashboardStatus Stop() => Run(false, OpStop);

    public DashboardStatus SetLed(bool on) => Run(false, OpSetLed, arg0: on ? 1u : 0u);

    public void Reboot()
    {
        lock (gate) RunReboot(OpReboot);
    }

    private void RunReboot(byte operation, byte flags = 0)
    {
        try { Perform(operation, flags: flags); }
        catch (Exception error) when (error is IOException or InvalidOperationException or InvalidDataException
            or UnauthorizedAccessException or NotSupportedException or TimeoutException)
        {
            throw new DeviceException($"HID transfer failed; outcome is unconfirmed: {error.Message}");
        }
        finally { Close(); }
    }

    public void SaveAndReboot(int profile, int address, int[] roles, bool acknowledgeOverclock)
    {
        lock (gate)
        {
            var current = EnsureIdentity();
            if (!current.CanSaveReboot || !current.CanSetClock)
                throw new DeviceException("update firmware to enable Save and Reboot");
            if (profile is < 0 or > 4 || (current.ClockProfileMask & 1 << profile) == 0)
                throw new ArgumentException("choose a clock profile the firmware advertises");
            if (address is < 8 or > 119 || roles.Length != 3 || roles.Any(role => role is < 0 or > 1))
                throw new ArgumentException("expected I2C address 8-119 and three bank roles");
            if (profile > 1 && !acknowledgeOverclock)
                throw new ArgumentException("acknowledge the overclock first");
            try
            {
                var flags = profile > 1 ? FlagAckOverclock : (byte)0;
                Perform(OpSetClockProfile, arg0: (uint)profile, flags: flags);
                Perform(OpSetI2cAddress, arg0: (uint)address);
                for (byte bank = 0; bank < 3; bank++)
                    Perform(OpSetBank, bank, (uint)(bank | roles[bank] << 8));
                RunReboot(OpSaveReboot, (byte)(FlagConfirmSave | flags));
            }
            catch (Exception error) when (error is DeviceException or IOException or InvalidOperationException
                or InvalidDataException or UnauthorizedAccessException or NotSupportedException or TimeoutException)
            {
                Close();
                throw new DeviceException($"Save and Reboot did not complete; target settings may have changed: {error.Message}");
            }
        }
    }

    public DashboardStatus SetBank(int bank, int backend, int role)
    {
        if (bank is < 0 or > 2 || backend is < 0 or > 2 || role is < 0 or > 1)
            throw new ArgumentException("bank, backend, or role is out of range");
        return Run(true, OpSetBank, (byte)bank, (uint)(backend | role << 8));
    }

    public DashboardStatus SetI2cAddress(int address)
    {
        if (address is < 0 or > 127)
            throw new ArgumentException("I2C address must be 0-127");
        return Run(true, OpSetI2cAddress, arg0: (uint)address);
    }

    public DashboardStatus SetClockProfile(int profile, bool acknowledgeOverclock)
    {
        int mask;
        lock (gate)
            mask = (identity ?? Status().Identity)?.ClockProfileMask ?? 0;
        if (profile is < 0 or > 4 || (mask & 1 << profile) == 0)
            throw new ArgumentException("choose a clock profile the firmware advertises");
        if (profile > 1 && !acknowledgeOverclock)
            throw new ArgumentException("acknowledge the overclock first");
        return Run(true, OpSetClockProfile, arg0: (uint)profile, flags: profile > 1 ? FlagAckOverclock : (byte)0);
    }

    public DashboardStatus SaveConfiguration(bool acknowledgeOverclock)
    {
        Configuration current;
        lock (gate)
            current = Status().Configuration ?? throw new DeviceException("read the configuration first");
        var overclock = current.ClockProfile > 1;
        if (overclock && !acknowledgeOverclock)
            throw new ArgumentException("acknowledge the overclock first");
        return Run(true, OpSaveConfig, flags: (byte)(FlagConfirmSave | (overclock ? FlagAckOverclock : 0)));
    }
}
