using System.Text.RegularExpressions;

namespace PicoPwm.Host;

public static class CdcResponseParser
{
    private static readonly Regex ChannelPattern = new(
        @"CH(?<channel>\d+): freq=(?<freq>\d+) Hz, duty=(?<duty>\d+)%(?:,)? pulses=(?<pulses>\d+)",
        RegexOptions.Compiled);
    private static readonly Regex SetPattern = new(
        @"OK CH(?<channel>\d+) freq=(?<freq>\d+) Hz duty=(?<duty>\d+)%",
        RegexOptions.Compiled);
    private static readonly Regex StatusRowPattern = new(
        @"^\s*(?<channel>\d+)\s+\S+\s+\S+", RegexOptions.Compiled);

    public static int CountStatusRows(IEnumerable<string> lines) => lines.Count(StatusRowPattern.IsMatch);

    public static ChannelState ParseChannel(string line)
    {
        var match = ChannelPattern.Match(line);
        if (!match.Success)
            throw new FormatException($"Unrecognized CDC channel response: {line}");
        return new ChannelState(int.Parse(match.Groups["channel"].Value),
            uint.Parse(match.Groups["freq"].Value), byte.Parse(match.Groups["duty"].Value),
            uint.Parse(match.Groups["pulses"].Value));
    }

    public static bool IsSetResponse(string line) => SetPattern.IsMatch(line);
}
