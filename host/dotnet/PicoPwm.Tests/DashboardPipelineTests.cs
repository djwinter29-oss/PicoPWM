// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Ji Dong

using System.Net;
using System.Net.Http.Json;
using System.Text.Json;
using Microsoft.AspNetCore.Mvc.Testing;

namespace PicoPwm.Tests;

public sealed class DashboardPipelineTests
{
    [Fact]
    public async Task RejectsAForeignOriginAndRequiresTheTokenOnTheHttpPipeline()
    {
        var previousToken = Environment.GetEnvironmentVariable("PICOPWM_TOKEN");
        var previousUrls = Environment.GetEnvironmentVariable("ASPNETCORE_URLS");
        var previousEnvironment = Environment.GetEnvironmentVariable("ASPNETCORE_ENVIRONMENT");
        Environment.SetEnvironmentVariable("ASPNETCORE_URLS", "http://127.0.0.1:5000");
        Environment.SetEnvironmentVariable("ASPNETCORE_ENVIRONMENT", "Production");
        try
        {
            Environment.SetEnvironmentVariable("PICOPWM_TOKEN", null);
            await using (var open = new WebApplicationFactory<Program>())
            {
                var client = open.CreateClient();
                Assert.Equal(HttpStatusCode.OK, (await client.GetAsync("/api/host")).StatusCode);
                using var foreign = new HttpRequestMessage(HttpMethod.Get, "/api/host");
                foreign.Headers.TryAddWithoutValidation("Origin", "http://evil.example");
                Assert.Equal(HttpStatusCode.Forbidden, (await client.SendAsync(foreign)).StatusCode);
                using var rebound = new HttpRequestMessage(HttpMethod.Get, "/api/host");
                rebound.Headers.Host = "evil.example";
                Assert.Equal(HttpStatusCode.Forbidden, (await client.SendAsync(rebound)).StatusCode);
            }

            Environment.SetEnvironmentVariable("PICOPWM_TOKEN", "secret");
            await using var locked = new WebApplicationFactory<Program>();
            var authed = locked.CreateClient();
            Assert.Equal(HttpStatusCode.Unauthorized, (await authed.GetAsync("/api/host")).StatusCode);
            var login = await authed.PostAsJsonAsync("/api/login", new { token = "secret" });
            Assert.Equal(HttpStatusCode.OK, login.StatusCode);
            Assert.Equal(HttpStatusCode.OK, (await authed.GetAsync("/api/host")).StatusCode);
        }
        finally
        {
            Environment.SetEnvironmentVariable("PICOPWM_TOKEN", previousToken);
            Environment.SetEnvironmentVariable("ASPNETCORE_URLS", previousUrls);
            Environment.SetEnvironmentVariable("ASPNETCORE_ENVIRONMENT", previousEnvironment);
        }
    }

    [Fact]
    public async Task ApiErrorsUseFixedSentences()
    {
        var previousToken = Environment.GetEnvironmentVariable("PICOPWM_TOKEN");
        var previousUrls = Environment.GetEnvironmentVariable("ASPNETCORE_URLS");
        var previousEnvironment = Environment.GetEnvironmentVariable("ASPNETCORE_ENVIRONMENT");
        Environment.SetEnvironmentVariable("PICOPWM_TOKEN", null);
        Environment.SetEnvironmentVariable("ASPNETCORE_URLS", "http://127.0.0.1:5000");
        Environment.SetEnvironmentVariable("ASPNETCORE_ENVIRONMENT", "Production");
        try
        {
            await using var app = new WebApplicationFactory<Program>();
            var client = app.CreateClient();
            var status = await client.GetAsync("/api/status");
            Assert.Equal(HttpStatusCode.OK, status.StatusCode);
            using (var body = JsonDocument.Parse(await status.Content.ReadAsStringAsync()))
            {
                Assert.False(body.RootElement.GetProperty("connected").GetBoolean());
                Assert.Equal(JsonValueKind.Null, body.RootElement.GetProperty("error").ValueKind);
            }

            var rejected = await client.PostAsJsonAsync("/api/stop?board=secret-board", new { });
            var text = await rejected.Content.ReadAsStringAsync();
            Assert.Equal(HttpStatusCode.Conflict, rejected.StatusCode);
            Assert.DoesNotContain("secret-board", text, StringComparison.Ordinal);
            Assert.DoesNotContain("unknown board", text, StringComparison.Ordinal);
            using var error = JsonDocument.Parse(text);
            Assert.Equal("Device request failed", error.RootElement.GetProperty("error").GetString());
        }
        finally
        {
            Environment.SetEnvironmentVariable("PICOPWM_TOKEN", previousToken);
            Environment.SetEnvironmentVariable("ASPNETCORE_URLS", previousUrls);
            Environment.SetEnvironmentVariable("ASPNETCORE_ENVIRONMENT", previousEnvironment);
        }
    }
}
