// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Ji Dong

using PicoPwm.Host;

namespace PicoPwm.Tests;

public sealed class DashboardHttpTests
{
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
