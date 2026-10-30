// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Ji Dong

using System.Security.Cryptography;
using System.Text;

namespace PicoPwm.Host;

/// <summary>Loopback bind rules and dashboard credentials for the local management API.</summary>
public static class DashboardBind
{
    public static bool IsLoopbackRequestHost(string? requestHost) =>
        !string.IsNullOrEmpty(requestHost) && IsLoopback(new HostString(requestHost).Host);

    public static bool IsLoopback(string host) =>
        host.Equals("127.0.0.1", StringComparison.OrdinalIgnoreCase)
        || host.Equals("localhost", StringComparison.OrdinalIgnoreCase)
        || host.Equals("::1", StringComparison.OrdinalIgnoreCase)
        || host.Equals("[::1]", StringComparison.OrdinalIgnoreCase);

    public static string DefaultUrl(string? host, string? port) =>
        $"http://{(string.IsNullOrWhiteSpace(host) ? "127.0.0.1" : host)}:{(string.IsNullOrWhiteSpace(port) ? "5000" : port)}";

    /// <summary>
    /// Prefer <c>--urls</c>, then <c>ASPNETCORE_URLS</c>, then the loopback default from
    /// <c>PICOPWM_HOST</c> and <c>PICOPWM_PORT</c>.
    /// </summary>
    public static string ListenUrls(string[] args, string? aspNetCoreUrls, string? host, string? port)
    {
        var urls = aspNetCoreUrls;
        for (var i = 0; i < args.Length; i++)
        {
            if (args[i] == "--urls" && i + 1 < args.Length)
                urls = args[i + 1];
            else if (args[i].StartsWith("--urls=", StringComparison.Ordinal))
                urls = args[i]["--urls=".Length..];
        }
        return string.IsNullOrWhiteSpace(urls) ? DefaultUrl(host, port) : urls;
    }

    public static bool UrlsWereConfigured(string[] args, string? aspNetCoreUrls) =>
        !string.IsNullOrWhiteSpace(aspNetCoreUrls)
        || args.Any(arg => arg == "--urls" || arg.StartsWith("--urls=", StringComparison.Ordinal));

    public static void RequireTokenForRemote(string urls, string? token)
    {
        foreach (var part in urls.Split(';', StringSplitOptions.RemoveEmptyEntries | StringSplitOptions.TrimEntries))
        {
            if (!Uri.TryCreate(part, UriKind.Absolute, out var uri) || (!IsLoopback(uri.Host) && string.IsNullOrEmpty(token)))
                throw new InvalidOperationException("Refusing to listen beyond loopback without PICOPWM_TOKEN.");
        }
    }

    public static bool OriginAllowed(string? origin, string requestHost)
    {
        if (string.IsNullOrEmpty(origin))
            return true;
        return Uri.TryCreate(origin, UriKind.Absolute, out var uri)
            && string.Equals(uri.Authority, requestHost, StringComparison.OrdinalIgnoreCase);
    }

    public static string CookieValue(string token) =>
        Convert.ToHexString(SHA256.HashData(Encoding.UTF8.GetBytes(token))).ToLowerInvariant();

    public static bool CredentialAccepted(string configured, string? authorization, string? cookie)
    {
        if (cookie is not null && FixedEqual(cookie, CookieValue(configured)))
            return true;
        const string prefix = "Bearer ";
        if (authorization is not null && authorization.StartsWith(prefix, StringComparison.Ordinal))
            return FixedEqual(authorization[prefix.Length..], configured);
        return false;
    }

    private static bool FixedEqual(string left, string right)
    {
        var a = Encoding.UTF8.GetBytes(left);
        var b = Encoding.UTF8.GetBytes(right);
        return a.Length == b.Length && CryptographicOperations.FixedTimeEquals(a, b);
    }
}
