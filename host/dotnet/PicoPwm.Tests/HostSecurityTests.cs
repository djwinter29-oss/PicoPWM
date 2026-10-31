// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Ji Dong

using PicoPwm.Host;

namespace PicoPwm.Tests;

public sealed class HostSecurityTests
{
    [Fact]
    public void DefaultsToLoopbackAndRejectsARemoteBindWithoutAToken()
    {
        Assert.Equal("http://127.0.0.1:5000", DashboardBind.ListenUrls([], null, null, null));
        Assert.Equal("http://10.0.0.8:5002", DashboardBind.ListenUrls(["--urls", "http://10.0.0.8:5002"],
            "http://127.0.0.1:5000", null, null));
        Assert.False(DashboardBind.UrlsWereConfigured([], null));
        Assert.True(DashboardBind.UrlsWereConfigured(["--urls=http://127.0.0.1:9"], null));
        DashboardBind.RequireTokenForRemote("http://127.0.0.1:5000;http://[::1]:5000", null);
        Assert.Throws<InvalidOperationException>(() => DashboardBind.RequireTokenForRemote("http://0.0.0.0:5000", ""));
        DashboardBind.RequireTokenForRemote("http://0.0.0.0:5000", "secret");
    }

    [Fact]
    public void RejectsAForeignOriginAndAcceptsTheTokenOrItsCookie()
    {
        Assert.True(DashboardBind.OriginAllowed(null, "127.0.0.1:5000"));
        Assert.True(DashboardBind.OriginAllowed("http://127.0.0.1:5000", "127.0.0.1:5000"));
        Assert.False(DashboardBind.OriginAllowed("http://evil.example", "127.0.0.1:5000"));
        Assert.False(DashboardBind.CredentialAccepted("secret", null, null));
        Assert.False(DashboardBind.CredentialAccepted("secret", "Bearer other", null));
        Assert.True(DashboardBind.CredentialAccepted("secret", "Bearer secret", null));
        Assert.True(DashboardBind.IsLoopbackRequestHost("127.0.0.1:5000"));
        Assert.True(DashboardBind.IsLoopbackRequestHost("[::1]:5000"));
        Assert.True(DashboardBind.IsLoopbackRequestHost("localhost"));
        Assert.False(DashboardBind.IsLoopbackRequestHost("evil.example"));
        Assert.False(DashboardBind.IsLoopbackRequestHost(null));
        Assert.True(DashboardBind.CredentialAccepted("secret", null, DashboardBind.CookieValue("secret")));
        Assert.False(DashboardBind.CredentialAccepted("secret", null, DashboardBind.CookieValue("other")));
    }

    [Fact]
    public void HttpResponsesDoNotContainDeviceExceptionText()
    {
        const string detail = "private device path /secret/device";
        var logged = new List<string>();
        var state = new DashboardStatus(true, null, detail, null, detail, [], [detail, null, detail], detail,
            null, detail, null, detail);
        var redacted = DashboardHttp.Redact(state, logged.Add);

        Assert.Equal(DashboardHttp.StatusUnavailable, redacted.Error);
        Assert.Equal(DashboardHttp.StatusUnavailable, redacted.IdentityError);
        Assert.Equal(DashboardHttp.StatusUnavailable, redacted.ConfigurationError);
        Assert.Equal(DashboardHttp.StatusUnavailable, redacted.RuntimeError);
        Assert.Equal(DashboardHttp.StatusUnavailable, redacted.PinsError);
        Assert.Equal(DashboardHttp.StatusUnavailable, redacted.BankErrors[0]);
        Assert.Null(redacted.BankErrors[1]);
        Assert.Equal(DashboardHttp.StatusUnavailable, redacted.BankErrors[2]);
        Assert.Equal(7, logged.Count);
        Assert.All(logged, line => Assert.Equal(detail, line));

        Assert.Equal((400, DashboardHttp.InvalidRequest), StatusOf(new ArgumentException(detail)));
        Assert.Equal((409, DashboardHttp.DeviceRequestFailed), StatusOf(new DeviceException(detail)));
        Assert.Equal((503, DashboardHttp.DeviceOperationFailed), StatusOf(new IOException(detail)));
        Assert.Equal((503, DashboardHttp.DeviceOperationFailed), StatusOf(new InvalidDataException(detail)));
        Assert.Equal((503, DashboardHttp.DeviceOperationFailed), StatusOf(new TimeoutException(detail)));
        Assert.Equal((500, DashboardHttp.InternalError), StatusOf(new InvalidCastException(detail)));
    }

    private static (int StatusCode, string Error) StatusOf(Exception error)
    {
        var failure = DashboardHttp.FromException(error);
        return (failure.StatusCode, failure.Error);
    }
}
