// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Ji Dong

using PicoPwm.Host;
using static PicoPwm.Tests.Fakes;

namespace PicoPwm.Tests;

public sealed class BoardRegistryTests
{
    [Fact]
    public void BoardRegistryPinsClientsAndPreservesIdentityAcrossReconnects()
    {
        BoardInfo[] discovered = [BoardInfo.Create("PicoPWM", "A", "/dev/a"),
            BoardInfo.Create("PicoPWM", "B", "/dev/b")];
        using var registry = new BoardRegistry(() => discovered, _ => Modern().Item2, serial: "B");
        var scan = registry.Boards();
        Assert.Equal(discovered[1].Id, scan.DefaultBoardId);
        var a = registry.Client(discovered[0].Id);
        Assert.NotSame(a, registry.Client(discovered[1].Id));
        Assert.Throws<DeviceException>(() => registry.Client("unknown"));
        var id = discovered[0].Id;
        discovered[0] = BoardInfo.Create("PicoPWM", "A", "/dev/new");
        Assert.Equal(id, registry.Boards().Boards[0].Id);
        Assert.Same(a, registry.Client(id));
        discovered = [];
        Assert.Empty(registry.Boards().Boards);
        Assert.Same(a, registry.Client(id));
        discovered = [BoardInfo.Create("PicoPWM", "same", "/a"), BoardInfo.Create("PicoPWM", "same", "/b")];
        Assert.Throws<DeviceException>(() => registry.Boards());
    }
}
