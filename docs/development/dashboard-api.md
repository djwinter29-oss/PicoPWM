# Dashboard Backend API

Internal HTTP contract for the shared page in `host/web/src/` and its Python/.NET
servers. Both servers implement it identically; it may change with the page.
For scripts, use the Python
[`PicoPwmHid` client](../../host/python/README.md#use-from-python) or the
[USB HID reports](../protocols/usb-hid.md) directly.

The server runs on the USB-connected computer, not the Pico. Requests can
drive outputs and save board configuration.

[Access](#access-rules) · [Board selection](#board-selection) ·
[Routes](#routes) · [Failures](#responses-and-failures)

## Access Rules

- The server listens on loopback by default. A non-loopback listen address
  requires `PICOPWM_TOKEN`; without a token, requests must use a loopback host.
- With a token, every `/api/*` route except `POST /api/login` requires either
  an `Authorization: Bearer <token>` header or the login cookie returned by
  `POST /api/login` with `{"token": "<token>"}`. Failures return HTTP 401. The
  page itself stays reachable so it can prompt for the token.
- Requests whose `Origin` differs from the request host are rejected.
- Python uses cookie `picopwm_python_auth` and .NET `picopwm_dotnet_auth`, so
  the two servers do not overwrite each other's login on one hostname.
- No TLS is provided.

## Board Selection

`GET /api/boards` returns `boards` (each with `id`, `product_name`,
`serial_number`, `device_path`) and `default_board_id`, which may be `null`.
Other requests pass the opaque ID as `board`. Selection stays pinned across
reconnects. Unselected status is disconnected and opens no HID device;
actions require a valid selection.

## Routes

Fields use snake_case. Actions require a JSON object (`{}` for no arguments)
and `Content-Type: application/json`. Required numbers never default to zero;
optional `acknowledge_overclock` defaults to `false`.
All routes except login, host, and boards take `?board=<id>`.

| Route | Body | Purpose |
| --- | --- | --- |
| `POST /api/login` | `{"token": "..."}` | Exchange the token for the login cookie |
| `GET /api/host` | | `{"dashboard_type": "Python"}` or `".NET"` |
| `GET /api/boards` | | Discover boards |
| `GET /api/status` | | Identity, configuration, telemetry, pins, and channels |
| `POST /api/channels/<channel>` | `{"freq_hz": 1000, "duty": 50}` | Set a generator channel |
| `POST /api/stop` | `{}` | Stop generator outputs |
| `POST /api/led` | `{"on": true}` | Set the identification LED |
| `POST /api/bank` | `{"bank": 0, "backend": 0, "role": 0}` | Stage a bank role |
| `POST /api/i2c-address` | `{"address": 64}` | Stage the I2C address (`8..119`) |
| `POST /api/clock-profile` | `{"profile": 0, "acknowledge_overclock": false}` | Stage a clock profile |
| `POST /api/save` | `{"acknowledge_overclock": false}` | Save staged settings without rebooting |
| `POST /api/reboot` | `{}` | Reboot the board |
| `POST /api/save-reboot` | see below | Stage, save, and reboot in one operation |

Channel IDs are `0..23`, frequency is an unsigned 32-bit Hz value, and duty is
`0..100`; periodic generation limits are 10 Hz-10 MHz for hardware PWM, 1
Hz-10 MHz for PIO, and 1 Hz-10 kHz for software PWM. A `0 Hz` request selects
a static output level. Firmware rejects writes to monitor channels; see the
[complete operating ranges](../guides/configuration.md#pwm-operating-ranges).
`bank` and `backend` are `0..2` (hardware PWM, PIO, software); role `0` is
generator and `1` is monitor. Clock profiles are `0..3`, limited to the
firmware's advertised `clock_profile_mask`; profiles above `1` require
`acknowledge_overclock`.

`/api/save-reboot` takes the complete target:

```json
{"profile": 0, "address": 64, "roles": [0, 1, 1], "acknowledge_overclock": false}
```

After validation, settings are sent before save/reboot. A later failure does
not roll back earlier settings; reread configuration before retrying.

## Responses and Failures

Status responses include `connected`, `identity`, `configuration`, `runtime`,
`pins`, and `channels`.

The examples below are excerpts; a full response also includes the other
status sections and all 24 channel entries. For a connected board with
successful reads:

```json
{
  "connected": true,
  "channels": [{"channel": 0, "freq_hz": 1000, "duty": 50}],
  "bank_errors": [null, null, null],
  "error": null
}
```

A partial bank-read failure keeps the connection but marks that bank's data
unavailable. Do not treat its channel entries as a fresh reading:

```json
{
  "connected": true,
  "channels": [{"channel": 8, "freq_hz": 1000, "duty": 50}],
  "bank_errors": ["Device status unavailable", null, null],
  "error": null
}
```

Other report failures use the corresponding `identity_error`,
`configuration_error`, `runtime_error`, or `pins_error` field. Device details
are logged by the host and replaced in HTTP responses with
`"Device status unavailable"`.

| HTTP status | Meaning |
| --- | --- |
| `200` | Route completed; inspect the response for connection and section errors |
| `400` | Invalid request fields or a missing JSON object |
| `401` | Missing or incorrect authentication when a token is configured |
| `403` | Disallowed request host or cross-origin request |
| `409` | Unknown board selection, rejected command, protocol error, or unconfirmed outcome |
| `500` | Unexpected server failure; the body is `Internal server error` |
| `503` | Discovery or unhandled transport/runtime failure |

Errors use user-safe `{"error": "description"}` messages. Actions usually
return refreshed status. HTTP 200 can still mean disconnected or unavailable
telemetry: inspect `connected`, `error`, section errors, and `bank_errors`.
`{"rebooting": true}` confirms the command, not USB reconnection.

Timeouts do not cancel submitted board commands. Reconnect and inspect state
before retrying mutations. Multi-channel actions can take several seconds.
