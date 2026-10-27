using System.Text.Json;
using System.Text.Json.Serialization;
using PicoPwm.Host;

var builder = WebApplication.CreateBuilder(new WebApplicationOptions
{
    Args = args,
    WebRootPath = Path.Combine(AppContext.BaseDirectory, "wwwroot"),
});
builder.Services.ConfigureHttpJsonOptions(options =>
{
    options.SerializerOptions.PropertyNamingPolicy = JsonNamingPolicy.SnakeCaseLower;
    options.SerializerOptions.DefaultIgnoreCondition = JsonIgnoreCondition.Never;
});
builder.Services.AddSingleton(_ => new BoardRegistry(HidDevices.Enumerate,
    board => new PicoPwmHid(() => HidDevices.Open(board.SerialNumber,
        board.SerialNumber is null ? board.DevicePath : null)),
    Environment.GetEnvironmentVariable("PICOPWM_SERIAL"), Environment.GetEnvironmentVariable("PICOPWM_DEVICE_PATH")));

var listenUrls = DashboardBind.ListenUrls(args, Environment.GetEnvironmentVariable("ASPNETCORE_URLS"),
    Environment.GetEnvironmentVariable("PICOPWM_HOST"), Environment.GetEnvironmentVariable("PICOPWM_PORT"));
DashboardBind.RequireTokenForRemote(listenUrls, Environment.GetEnvironmentVariable("PICOPWM_TOKEN"));
if (!DashboardBind.UrlsWereConfigured(args, Environment.GetEnvironmentVariable("ASPNETCORE_URLS")))
    builder.WebHost.UseUrls(listenUrls);

var app = builder.Build();
app.UseDefaultFiles();
app.UseStaticFiles();
app.Use(async (context, next) =>
{
    if (!DashboardBind.OriginAllowed(context.Request.Headers.Origin.ToString(), context.Request.Host.Value ?? ""))
    {
        context.Response.StatusCode = StatusCodes.Status403Forbidden;
        await context.Response.WriteAsJsonAsync(new { error = "cross-origin request rejected" });
        return;
    }

    var configured = Environment.GetEnvironmentVariable("PICOPWM_TOKEN");
    var path = context.Request.Path.Value ?? "";
    var isApi = path.Equals("/api", StringComparison.OrdinalIgnoreCase)
        || path.StartsWith("/api/", StringComparison.OrdinalIgnoreCase);
    var isLogin = path.Equals("/api/login", StringComparison.OrdinalIgnoreCase);
    if (!string.IsNullOrEmpty(configured) && isApi && !isLogin)
    {
        context.Request.Cookies.TryGetValue("picopwm_auth", out var cookie);
        if (!DashboardBind.CredentialAccepted(configured, context.Request.Headers.Authorization.ToString(), cookie))
        {
            context.Response.StatusCode = StatusCodes.Status401Unauthorized;
            await context.Response.WriteAsJsonAsync(new { error = "unauthorized" });
            return;
        }
    }

    await next();
});

IResult Act(Func<object> call)
{
    try
    {
        return Results.Ok(call());
    }
    catch (ArgumentException error)
    {
        return Results.BadRequest(new { error = error.Message });
    }
    catch (DeviceException error)
    {
        return Results.Conflict(new { error = error.Message });
    }
    catch (Exception error) when (error is IOException or InvalidOperationException or UnauthorizedAccessException
        or NotSupportedException or TimeoutException)
    {
        return Results.Problem(error.Message, statusCode: 503);
    }
}

app.MapPost("/api/login", (HttpContext http, LoginRequest request) =>
{
    var configured = Environment.GetEnvironmentVariable("PICOPWM_TOKEN");
    if (string.IsNullOrEmpty(configured))
        return Results.NotFound(new { error = "authentication is disabled" });
    if (string.IsNullOrEmpty(request.Token)
        || !DashboardBind.CredentialAccepted(configured, "Bearer " + request.Token, null))
        return Results.Json(new { error = "unauthorized" }, statusCode: StatusCodes.Status401Unauthorized);
    http.Response.Cookies.Append("picopwm_auth", DashboardBind.CookieValue(configured), new CookieOptions
    {
        HttpOnly = true,
        SameSite = SameSiteMode.Strict,
        IsEssential = true,
    });
    return Results.Ok(new { ok = true });
});
app.MapGet("/api/host", () => new { dashboard_type = ".NET" });
app.MapGet("/api/boards", (BoardRegistry boards) => Act(() => boards.Boards()));
app.MapGet("/api/status", (string? board, BoardRegistry boards) => Act(() => board is null
    ? new DashboardStatus(false, null, null, null, null, [], new string?[3], null)
    : boards.Client(board).Status()));
app.MapPost("/api/channels/{channel:int}", (int channel, string? board, ChannelRequest request, BoardRegistry boards) =>
    Act(() => boards.Client(board).SetChannel(channel, request.FreqHz, request.Duty)));
app.MapPost("/api/stop", (string? board, BoardRegistry boards) => Act(() => boards.Client(board).Stop()));
app.MapPost("/api/bank", (string? board, BankRequest request, BoardRegistry boards) =>
    Act(() => boards.Client(board).SetBank(request.Bank, request.Backend, request.Role)));
app.MapPost("/api/i2c-address", (string? board, AddressRequest request, BoardRegistry boards) =>
    Act(() => boards.Client(board).SetI2cAddress(request.Address)));
app.MapPost("/api/clock-profile", (string? board, ClockRequest request, BoardRegistry boards) =>
    Act(() => boards.Client(board).SetClockProfile(request.Profile, request.AcknowledgeOverclock)));
app.MapPost("/api/save", (string? board, SaveRequest request, BoardRegistry boards) =>
    Act(() => boards.Client(board).SaveConfiguration(request.AcknowledgeOverclock)));
app.MapPost("/api/led", (string? board, LedRequest request, BoardRegistry boards) =>
    Act(() => boards.Client(board).SetLed(request.On ?? throw new ArgumentException("on must be a boolean"))));
app.MapPost("/api/reboot", (string? board, BoardRegistry boards) => Act(() =>
{
    boards.Client(board).Reboot();
    return new { rebooting = true };
}));
app.MapPost("/api/save-reboot", (string? board, BootRequest request, BoardRegistry boards) => Act(() =>
{
    boards.Client(board).SaveAndReboot(request.Profile, request.Address,
        request.Roles ?? throw new ArgumentException("roles must be a list of three bank roles"),
        request.AcknowledgeOverclock);
    return new { rebooting = true };
}));

app.Run();

record LoginRequest(string? Token);
record ChannelRequest(long FreqHz, int Duty);
record BankRequest(int Bank, int Backend, int Role);
record AddressRequest(int Address);
record ClockRequest(int Profile, bool AcknowledgeOverclock = false);
record SaveRequest(bool AcknowledgeOverclock = false);
record LedRequest(bool? On);
record BootRequest(int Profile, int Address, int[]? Roles, bool AcknowledgeOverclock = false);
