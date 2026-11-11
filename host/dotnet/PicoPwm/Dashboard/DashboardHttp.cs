// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Ji Dong

namespace PicoPwm.Host;

/// <summary>HTTP boundary for dashboard results. Device and parser text stays in server logs.</summary>
public static class DashboardHttp
{
    public const string StatusUnavailable = "Device status unavailable";
    public const string InvalidRequest = "Invalid request fields";
    public const string DeviceRequestFailed = "Device request failed";
    public const string DeviceOperationFailed = "Device operation failed";
    public const string InternalError = "Internal server error";

    public readonly record struct Failure(int StatusCode, string Error);

    /// <summary>Copy a handler result, replacing status error text with <see cref="StatusUnavailable"/>.</summary>
    public static object ForWire(object value, Action<string>? logDetail = null)
    {
        if (value is DashboardStatus state)
            return Redact(state, logDetail);
        return value;
    }

    public static DashboardStatus Redact(DashboardStatus state, Action<string>? logDetail = null) => state with
    {
        IdentityError = Hide(state.IdentityError, logDetail),
        ConfigurationError = Hide(state.ConfigurationError, logDetail),
        RuntimeError = Hide(state.RuntimeError, logDetail),
        PinsError = Hide(state.PinsError, logDetail),
        Error = Hide(state.Error, logDetail),
        BankErrors = state.BankErrors.Select(error => Hide(error, logDetail)).ToArray(),
    };

    /// <summary>Map a handler exception to a fixed sentence. The caller logs the original exception.</summary>
    public static Failure FromException(Exception error) => error switch
    {
        ArgumentException => new(StatusCodes.Status400BadRequest, InvalidRequest),
        DeviceException => new(StatusCodes.Status409Conflict, DeviceRequestFailed),
        IOException or InvalidOperationException or UnauthorizedAccessException or NotSupportedException
            or TimeoutException or InvalidDataException => new(StatusCodes.Status503ServiceUnavailable, DeviceOperationFailed),
        _ => new(StatusCodes.Status500InternalServerError, InternalError),
    };

    private static string? Hide(string? detail, Action<string>? logDetail)
    {
        if (detail is null)
            return null;
        logDetail?.Invoke(detail);
        return StatusUnavailable;
    }
}
