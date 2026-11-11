// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Ji Dong

using PicoPwm.Host;
using static PicoPwm.Tests.Fakes;

namespace PicoPwm.Tests;

public sealed class ProtocolTests
{
    [Fact]
    public void DecodesDocumentedReports()
    {
        Assert.Equal("RP2350", Protocol.DecodeIdentity(Identity).Mcu);
        Assert.Equal("0102030405060708", Protocol.DecodeIdentity(Identity).SerialNumber);
        var rebootIdentity = (byte[])Identity.Clone();
        rebootIdentity[16] = 127;
        Assert.True(Protocol.DecodeIdentity(rebootIdentity).CanSave);
        var config = Protocol.DecodeConfiguration(Configuration);
        Assert.Equal(150_000_000u, config.SystemClockHz);
        Assert.Equal(new Protocol.ChannelState(8, 1000, 50), Protocol.DecodeBankStatus(Bank, 1)[0]);
    }

    [Fact]
    public void RejectsBadLayouts()
    {
        Assert.Throws<InvalidDataException>(() => Protocol.DecodeIdentity(Identity[..^1]));
        Assert.Throws<InvalidDataException>(() => Protocol.DecodeActionResult([1, 1, 1, 9, 0, 0, 0, 0, 0, 0, 0, 0]));
    }

    [Fact]
    public void EncodesSetChannel() => Assert.Equal(
        new byte[] { 1, 7, 1, 9, 0xE8, 3, 0, 0, 50, 0, 0, 0 },
        Protocol.EncodeActionRequest(7, Protocol.OpSetChannel, 9, 1000, 50));
}
