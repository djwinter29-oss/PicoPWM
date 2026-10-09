namespace PicoPwm.Host;

public sealed record ChannelState(
    int Channel,
    uint FrequencyHz,
    byte DutyPercent,
    uint PulseCount)
{
    public bool Enabled => FrequencyHz > 0;
}

public sealed record ChannelUpdate(uint FrequencyHz, byte DutyPercent);

public interface IPicoPwmBackend : IDisposable
{
    int ChannelCount { get; }
    ChannelState GetChannel(int channel);
    ChannelState SetChannel(int channel, uint frequencyHz, byte dutyPercent);
    void StopAll();
}
