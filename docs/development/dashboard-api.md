# Dashboard Backend API

The Python and .NET dashboards serve the same browser page from
`host/web/src/`. That page talks to its local server over this HTTP API; both
servers must implement it identically. It is an internal contract between the
shared page and its two backends, not a supported scripting interface, and may
change with the dashboards. Scripts should use the Python
[`PicoPwmHid` client](../../host/python/README.md#use-from-python) or the
[USB HID reports](../protocols/usb-hid.md) directly.

The server runs on the computer connected to the board over USB HID, not on
the Pico. Requests can change physical outputs or persist board configuration.

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
Other requests pass the chosen ID as the `board` query parameter. IDs are
opaque; a client stays pinned to its board across reconnects and never
silently selects another. Status without a selection is disconnected and does
not open a HID device; actions without a valid selection are rejected.

## Routes

Fields use snake_case. Actions take a JSON object body (`{}` when there are no
arguments) with `Content-Type: application/json`. Required numeric fields are
never defaulted to zero; `acknowledge_overclock` is optional and defaults to
`false`. Every route except login, host, and boards takes `?board=<id>`.

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
`0..100`; firmware applies the [bank limits](../design/pwm/banks.md) and
rejects writes to monitor channels. `bank` and `backend` are `0..2` (hardware
PWM, PIO, software); role `0` is generator and `1` is monitor. Clock profiles
are `0..3`, limited to the firmware's advertised `clock_profile_mask`; profiles
above `1` require `acknowledge_overclock`.

`/api/save-reboot` takes the complete target:

```json
{"profile": 0, "address": 64, "roles": [0, 1, 1], "acknowledge_overclock": false}
```

The server validates it, then sends each setting and the final save/reboot
command. Settings already sent are not rolled back if a later step fails, so
the page rereads configuration before retrying.

## Responses and Failures

Status responses include `connected`, `identity`, `configuration`, `runtime`,
`pins`, and `channels`.

| HTTP status | Meaning |
| --- | --- |
| `200` | Route completed; inspect the response for connection and section errors |
| `400` | Invalid request fields or a missing JSON object |
| `401` | Missing or incorrect authentication when a token is configured |
| `403` | Disallowed request host or cross-origin request |
| `409` | Unknown board selection, rejected command, protocol error, or unconfirmed outcome |
| `500` | Unexpected server failure; the body is `Internal server error` |
| `503` | Discovery or unhandled transport/runtime failure |

Errors use a stable, user-safe `{"error": "description"}` message. Most
successful actions return refreshed status, though its sections may be
unavailable. A status request can return HTTP 200 with `connected: false`;
even a connected board may have invalid or unavailable telemetry. Check
`connected`, `error`, section errors, and `bank_errors`, not only the HTTP
status. Reboot and save/reboot return `{"rebooting": true}` only after command
confirmation; this does not mean USB reconnection has finished.

Do not blindly retry mutations after a timeout or disconnect. A client timeout
does not cancel a command already submitted to the board; reconnect and inspect
the state first. Multi-channel actions can take several seconds.
