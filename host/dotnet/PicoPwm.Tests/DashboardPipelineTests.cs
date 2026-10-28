using System.Net;
using System.Net.Http.Json;
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
}
