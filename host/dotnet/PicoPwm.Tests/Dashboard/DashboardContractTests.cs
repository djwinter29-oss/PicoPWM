// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Ji Dong

using PicoPwm.Host;
using static PicoPwm.Tests.Fakes;
using System.Text.Json;

namespace PicoPwm.Tests;

public sealed class DashboardContractTests
{
    [Fact]
    public void DashboardJsonMatchesTheSharedBrowserContract()
    {
        using var json = JsonDocument.Parse(JsonSerializer.Serialize(Modern().Item2.Status(),
            new JsonSerializerOptions { PropertyNamingPolicy = JsonNamingPolicy.SnakeCaseLower }));
        var root = json.RootElement;
        Assert.True(root.GetProperty("identity").GetProperty("can_save_reboot").GetBoolean());
        Assert.Equal("Pico 2", root.GetProperty("identity").GetProperty("board_model").GetString());
        Assert.Equal(532480, root.GetProperty("identity").GetProperty("ram_total_bytes").GetInt32());
        Assert.Equal(22.9, root.GetProperty("runtime").GetProperty("temperature_c").GetDouble());
        Assert.Equal(11.7, root.GetProperty("runtime").GetProperty("core0_load").GetDouble());
        Assert.True(root.GetProperty("configuration").GetProperty("led_on").GetBoolean());
        Assert.Equal(26, root.GetProperty("pins").GetProperty("i2c_sda").GetInt32());
        Assert.Equal(32, root.GetProperty("configuration").GetProperty("target").GetProperty("i2c_address").GetInt32());
    }
}
