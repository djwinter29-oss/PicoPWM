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
}
