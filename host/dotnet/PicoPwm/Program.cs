using PicoPwm.Host;

var builder = WebApplication.CreateBuilder(args);
builder.Services.AddSingleton<IPicoPwmBackend>(_ => BackendFactory.Create());

var app = builder.Build();
app.UseDefaultFiles();
app.UseStaticFiles();

app.MapGet("/api/channels", (IPicoPwmBackend backend) =>
{
    try
    {
        return Results.Ok(Enumerable.Range(0, backend.ChannelCount).Select(backend.GetChannel));
    }
    catch (Exception exception)
    {
        return Results.Problem(exception.Message, statusCode: 503);
    }
});

app.MapPost("/api/channels/{channel:int}", (int channel, ChannelUpdate update, IPicoPwmBackend backend) =>
{
    if (channel < 0 || channel >= backend.ChannelCount)
        return Results.BadRequest(new { error = $"channel must be between 0 and {backend.ChannelCount - 1}" });
    if (update.DutyPercent > 100)
        return Results.BadRequest(new { error = "duty must be between 0 and 100" });

    try
    {
        return Results.Ok(backend.SetChannel(channel, update.FrequencyHz, update.DutyPercent));
    }
    catch (ArgumentOutOfRangeException exception)
    {
        return Results.BadRequest(new { error = exception.Message });
    }
    catch (Exception exception)
    {
        return Results.Problem(exception.Message, statusCode: 503);
    }
});

app.MapPost("/api/stop", (IPicoPwmBackend backend) =>
{
    try
    {
        backend.StopAll();
        return Results.Ok();
    }
    catch (Exception exception)
    {
        return Results.Problem(exception.Message, statusCode: 503);
    }
});

app.Run();

static class BackendFactory
{
    public static IPicoPwmBackend Create()
    {
        var transport = Environment.GetEnvironmentVariable("PICOPWM_TRANSPORT")?.ToLowerInvariant() ?? "cdc";
        return transport switch
        {
            "cdc" => new CdcBackend(Environment.GetEnvironmentVariable("PICOPWM_CDC_PORT") ?? "/dev/ttyACM0"),
            "i2c" => new I2cBackend(
                GetInt("PICOPWM_I2C_BUS", 1),
                GetInt("PICOPWM_I2C_ADDRESS", 0x40)),
            _ => throw new InvalidOperationException("PICOPWM_TRANSPORT must be 'cdc' or 'i2c'")
        };
    }

    private static int GetInt(string name, int fallback)
    {
        var value = Environment.GetEnvironmentVariable(name);
        if (value is null)
            return fallback;
        var isHex = value.StartsWith("0x", StringComparison.OrdinalIgnoreCase);
        return Convert.ToInt32(isHex ? value[2..] : value, isHex ? 16 : 10);
    }
}
