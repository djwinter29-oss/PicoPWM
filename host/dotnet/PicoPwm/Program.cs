using System.Text.Json;
using System.Text.Json.Serialization;
using PicoPwm.Host;

var builder = WebApplication.CreateBuilder(args);
builder.Services.ConfigureHttpJsonOptions(options =>
{
    options.SerializerOptions.PropertyNamingPolicy = JsonNamingPolicy.SnakeCaseLower;
    options.SerializerOptions.DefaultIgnoreCondition = JsonIgnoreCondition.Never;
});
builder.Services.AddSingleton(_ => new PicoPwmHid(() => HidDevices.Open(
    Environment.GetEnvironmentVariable("PICOPWM_SERIAL"), Environment.GetEnvironmentVariable("PICOPWM_DEVICE_PATH"))));

var app = builder.Build();
app.UseDefaultFiles();
app.UseStaticFiles();

IResult Act(Func<DashboardStatus> call)
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
}

app.MapGet("/api/status", (PicoPwmHid hid) => hid.Status());
app.MapPost("/api/channels/{channel:int}", (int channel, ChannelRequest request, PicoPwmHid hid) =>
    Act(() => hid.SetChannel(channel, request.FreqHz, request.Duty)));
app.MapPost("/api/stop", (PicoPwmHid hid) => Act(hid.Stop));
app.MapPost("/api/bank", (BankRequest request, PicoPwmHid hid) =>
    Act(() => hid.SetBank(request.Bank, request.Backend, request.Role)));
app.MapPost("/api/i2c-address", (AddressRequest request, PicoPwmHid hid) =>
    Act(() => hid.SetI2cAddress(request.Address)));
app.MapPost("/api/clock-profile", (ClockRequest request, PicoPwmHid hid) =>
    Act(() => hid.SetClockProfile(request.Profile, request.AcknowledgeOverclock)));
app.MapPost("/api/save", (SaveRequest request, PicoPwmHid hid) =>
    Act(() => hid.SaveConfiguration(request.AcknowledgeOverclock)));

app.Run();

record ChannelRequest(long FreqHz, int Duty);
record BankRequest(int Bank, int Backend, int Role);
record AddressRequest(int Address);
record ClockRequest(int Profile, bool AcknowledgeOverclock = false);
record SaveRequest(bool AcknowledgeOverclock = false);
