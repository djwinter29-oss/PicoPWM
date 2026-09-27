# PicoPWM .NET host

This ASP.NET Core application provides a C# management interface for all 24
PicoPWM logical channels. It supports the same firmware transports as the
Python host tool:

- USB CDC at 115200 baud through `System.IO.Ports`
- Linux I2C through `/dev/i2c-*` and the documented register map

## Requirements

- .NET 8 SDK
- PicoPWM firmware connected over USB CDC or Linux I2C

## Run over USB CDC

```sh
cd host/dotnet
PICOPWM_TRANSPORT=cdc PICOPWM_CDC_PORT=/dev/ttyACM0 dotnet run --project src/PicoPwm.Host
```

## Run over I2C

```sh
cd host/dotnet
PICOPWM_TRANSPORT=i2c PICOPWM_I2C_BUS=1 PICOPWM_I2C_ADDRESS=0x40 dotnet run --project src/PicoPwm.Host
```

Open <http://localhost:5000>. The dashboard reads the realized state of every
channel, applies frequency and duty updates, and exposes stop-all.

The API endpoints are:

- `GET /api/channels`
- `POST /api/channels/{channel}` with `{"frequencyHz": 1000, "dutyPercent": 50}`
- `POST /api/stop`

The I2C backend is Linux-specific because it uses the kernel I2C device ioctl;
the CDC backend is portable to platforms supported by `System.IO.Ports`.

Build and test the complete solution with:

```sh
dotnet build PicoPwm.sln
dotnet test PicoPwm.sln
```
