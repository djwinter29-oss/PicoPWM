// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Ji Dong

using System.Net;
using System.Net.Http.Json;
using System.Text.Json;
using Microsoft.AspNetCore.Mvc.Testing;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Logging;
using PicoPwm.Host;

namespace PicoPwm.Tests;

public sealed class DashboardPipelineTests
{
    [Fact]
    public async Task MissingActionFieldsAreRejectedWithoutHardwareWritesAndHostReportsVersion()
    {
        var previousToken = Environment.GetEnvironmentVariable("PICOPWM_TOKEN");
        var previousUrls = Environment.GetEnvironmentVariable("ASPNETCORE_URLS");
        var previousEnvironment = Environment.GetEnvironmentVariable("ASPNETCORE_ENVIRONMENT");
        Environment.SetEnvironmentVariable("PICOPWM_TOKEN", null);
        Environment.SetEnvironmentVariable("ASPNETCORE_URLS", "http://127.0.0.1:5000");
        Environment.SetEnvironmentVariable("ASPNETCORE_ENVIRONMENT", "Production");
        try
        {
            var board = BoardInfo.Create("PicoPWM", "test", "/fake");
            var (device, hid) = Fakes.Modern();
            using var registry = new BoardRegistry(() => [board], _ => hid);
            registry.Boards();
            await using var app = new WebApplicationFactory<Program>().WithWebHostBuilder(builder =>
                builder.ConfigureServices(services => services.AddSingleton(registry)));
            var client = app.CreateClient();
            var cases = new (string Route, object Body)[]
            {
                ("channels/0", new { }),
                ("channels/0", new { freq_hz = 1000 }),
                ("channels/0", new { duty = 50 }),
                ("channels/0", new { freq_hz = (long?)null, duty = 50 }),
                ("bank", new { backend = 0, role = 0 }),
                ("bank", new { bank = 0, role = 0 }),
                ("bank", new { bank = 0, backend = 0 }),
                ("i2c-address", new { }),
                ("clock-profile", new { }),
                ("led", new { }),
                ("led", new { on = (bool?)null }),
                ("save-reboot", new { address = 64, roles = new[] { 0, 1, 1 } }),
                ("save-reboot", new { profile = 0, roles = new[] { 0, 1, 1 } }),
                ("save-reboot", new { profile = 0, address = 64 }),
                ("save-reboot", new { profile = 0, address = 64, roles = (int[]?)null }),
            };
            foreach (var (route, body) in cases)
            {
                var response = await client.PostAsJsonAsync($"/api/{route}?board={board.Id}", body);
                Assert.Equal(HttpStatusCode.BadRequest, response.StatusCode);
                Assert.Empty(device.Sent);
            }
            var valid = await client.PostAsJsonAsync($"/api/channels/0?board={board.Id}", new { freq_hz = 0, duty = 0 });
            Assert.Equal(HttpStatusCode.OK, valid.StatusCode);
            Assert.Single(device.Sent);
            using var metadata = JsonDocument.Parse(await client.GetStringAsync("/api/host"));
            var expected = typeof(Program).Assembly.GetCustomAttributes(
                typeof(System.Reflection.AssemblyInformationalVersionAttribute), false)
                .OfType<System.Reflection.AssemblyInformationalVersionAttribute>().Single().InformationalVersion;
            Assert.Equal(".NET", metadata.RootElement.GetProperty("dashboard_type").GetString());
            Assert.Equal(expected, metadata.RootElement.GetProperty("version").GetString());
        }
        finally
        {
            Environment.SetEnvironmentVariable("PICOPWM_TOKEN", previousToken);
            Environment.SetEnvironmentVariable("ASPNETCORE_URLS", previousUrls);
            Environment.SetEnvironmentVariable("ASPNETCORE_ENVIRONMENT", previousEnvironment);
        }
    }

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
            Assert.StartsWith("picopwm_dotnet_auth=", Assert.Single(login.Headers.GetValues("Set-Cookie")));
            Assert.Equal(HttpStatusCode.OK, (await authed.GetAsync("/api/host")).StatusCode);
            var isolated = locked.CreateClient(new WebApplicationFactoryClientOptions { HandleCookies = false });
            var value = PicoPwm.Host.DashboardBind.CookieValue("secret");
            isolated.DefaultRequestHeaders.Add("Cookie", $"picopwm_python_auth={value}; picopwm_auth={value}");
            Assert.Equal(HttpStatusCode.Unauthorized, (await isolated.GetAsync("/api/host")).StatusCode);
            isolated.DefaultRequestHeaders.Remove("Cookie");
            isolated.DefaultRequestHeaders.Add("Cookie", $"picopwm_python_auth=other; picopwm_dotnet_auth={value}");
            Assert.Equal(HttpStatusCode.OK, (await isolated.GetAsync("/api/host")).StatusCode);
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
            var loggers = app.Services.GetRequiredService<ILoggerFactory>();
            foreach (var category in new[]
            {
                "Microsoft.AspNetCore.Hosting.Diagnostics",
                "Microsoft.AspNetCore.Routing.EndpointMiddleware",
                "Microsoft.AspNetCore.Http.Result.OkObjectResult",
            })
            {
                var logger = loggers.CreateLogger(category);
                Assert.False(logger.IsEnabled(LogLevel.Information));
                Assert.True(logger.IsEnabled(LogLevel.Warning));
                Assert.True(logger.IsEnabled(LogLevel.Error));
            }
            Assert.True(loggers.CreateLogger("Microsoft.Hosting.Lifetime").IsEnabled(LogLevel.Information));
            Assert.True(loggers.CreateLogger("Program").IsEnabled(LogLevel.Warning));
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
