using System.IO.Ports;

namespace PicoPwm.Host;

public sealed class CdcBackend : IPicoPwmBackend
{
    private readonly SerialPort _serial;
    private readonly TimeSpan _timeout;

    public CdcBackend(string port, int baudRate = 115200, int timeoutMilliseconds = 2000)
    {
        _timeout = TimeSpan.FromMilliseconds(timeoutMilliseconds);
        _serial = new SerialPort(port, baudRate) { ReadTimeout = timeoutMilliseconds, NewLine = "\n" };
        _serial.Open();
        ReadUntilPrompt();
    }

    public int ChannelCount => CdcResponseParser.CountStatusRows(Command("status"));

    public ChannelState GetChannel(int channel)
    {
        var lines = Command($"get {channel}");
        var line = LastResponse(lines);
        var state = CdcResponseParser.ParseChannel(line);
        return state with { Channel = channel };
    }

    public ChannelState SetChannel(int channel, uint frequencyHz, byte dutyPercent)
    {
        var line = LastResponse(Command($"set {channel} {frequencyHz} {dutyPercent}"));
        if (!CdcResponseParser.IsSetResponse(line))
            throw new InvalidOperationException($"CDC set failed: {line}");
        return GetChannel(channel);
    }

    public void StopAll()
    {
        var line = LastResponse(Command("stop"));
        if (line.StartsWith("ERR", StringComparison.Ordinal))
            throw new InvalidOperationException(line);
    }

    public void Dispose() => _serial.Dispose();

    private List<string> Command(string command)
    {
        _serial.WriteLine(command);
        var output = ReadUntilPrompt();
        return output.Split('\n', StringSplitOptions.RemoveEmptyEntries)
            .Select(line => line.Trim())
            .Where(line => line.Length > 0 && line != "pico>")
            .ToList();
    }

    private string ReadUntilPrompt()
    {
        var deadline = DateTime.UtcNow + _timeout;
        var output = new System.Text.StringBuilder();
        while (!output.ToString().Contains("pico> ", StringComparison.Ordinal))
        {
            if (DateTime.UtcNow >= deadline)
                throw new TimeoutException("Timed out waiting for PicoPWM CDC prompt");
            output.Append(_serial.ReadExisting());
            if (output.Length == 0)
                Thread.Sleep(5);
        }

        return output.ToString().Replace("\r\n", "\n").Replace('\r', '\n');
    }

    private static string LastResponse(IReadOnlyList<string> lines)
    {
        if (lines.Count == 0)
            throw new InvalidOperationException("PicoPWM CDC returned no response");
        var line = lines[^1];
        if (line.StartsWith("ERR", StringComparison.Ordinal))
            throw new InvalidOperationException(line);
        return line;
    }
}
