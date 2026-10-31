# Python API and Development Guide

Use this guide to integrate scripts and applications with the PicoPWM Python
dashboard. For installation, supported platforms, hardware precautions, and
direct HID client examples, start with the [Python README](../../host/python/README.md).

The HTTP API runs on the computer connected to the board over USB HID. The
default base URL is `http://127.0.0.1:5000`; it is not an endpoint on the Pico
itself. API requests can change physical outputs or persist board configuration.

## Authentication and Network Access

The dashboard defaults to loopback. A non-loopback listen address requires
`PICOPWM_TOKEN`. When a token is configured, the dashboard prompts for it.
API clients can send an `Authorization` header with the `Bearer` scheme followed
by the configured token, or authenticate through `POST /api/login` with
`{"token": "<token>"}` to receive a login cookie. Requests with an `Origin` that
differs from the request host are rejected.

The built-in server does not provide TLS. Do not expose it directly to the public
internet; use a trusted network and an appropriately configured HTTPS reverse
proxy if remote access is needed.

When no token is configured, requests must use a loopback host. With a token,
all `/api/*` routes except `/api/login` require authentication, including reads
and discovery. A missing or incorrect credential returns HTTP 401.
The dashboard page remains accessible so it can prompt for a token.

When embedding Flask with `create_app()`, pass `token=...` explicitly; the factory
does not read `PICOPWM_TOKEN` itself. The `picopwm` entry point reads that
environment variable and passes it to the factory.

## HTTP API

### Discover and Select a Board

1. Call `GET /api/boards`.
2. Select an entry from the returned `boards` array and retain its `id`.
   `default_board_id` indicates the preferred selection, or may be `null`.
3. Pass that ID as the `board` query parameter for status reads and actions.

Board entries contain `id`, `product_name`, `serial_number`, and `device_path`.
Treat IDs as opaque identifiers, not array indexes. Clients remain pinned to the
selected board across reconnects; do not silently select a different board when
one disappears.

For example, read the first discovered board using the Python standard library.
This example assumes the default loopback server without a configured token:

```python
import json
from urllib.parse import urlencode
from urllib.request import urlopen

base_url = "http://127.0.0.1:5000"
with urlopen(f"{base_url}/api/boards", timeout=5) as response:
    discovery = json.load(response)
if not discovery["boards"]:
    raise RuntimeError("No PicoPWM boards found")

board_id = discovery["boards"][0]["id"]
query = urlencode({"board": board_id})
with urlopen(f"{base_url}/api/status?{query}", timeout=5) as response:
    state = json.load(response)
if not state["connected"]:
    raise RuntimeError(state["error"])
print(state["channels"])
```

In an interactive application, let the user choose a board rather than relying
on discovery order.

### Routes and Request Bodies

Responses and request fields use snake_case. Send action requests with
`Content-Type: application/json` and a JSON object, including `{}` for commands
without arguments.

| Endpoint | Purpose |
| --- | --- |
| `GET /api/boards` | Discover boards and obtain board IDs |
| `GET /api/host` | Identify the dashboard implementation |
| `GET /api/status?board=<id>` | Read identity, configuration, telemetry, pins, and channels |
| `POST /api/channels/<channel>?board=<id>` | Set frequency and duty using `{"freq_hz": 1000, "duty": 50}` |
| `POST /api/stop?board=<id>` | Stop generator outputs; body `{}` |
| `POST /api/led?board=<id>` | Set the identification LED using `{"on": true}` |
| `POST /api/reboot?board=<id>` | Reboot the board; body `{}` |
| `POST /api/save-reboot?board=<id>` | Validate, save, and reboot with target configuration |

Use board IDs returned by discovery. Board actions require an explicit selection;
unknown or missing selections are rejected. Status without a selection is
disconnected and does not open a HID device.

Channel IDs are `0..23`, frequency is an unsigned 32-bit integer in Hz, and duty
is an integer percent in `0..100`. Firmware applies backend-specific frequency
limits and rejects writes to monitor channels. Read realized state after a
mutation rather than assuming it exactly matches the request.

### Save and Reboot

`POST /api/save-reboot?board=<id>` accepts:

```json
{
  "profile": 0,
  "address": 64,
  "roles": [0, 1, 1],
  "acknowledge_overclock": false
}
```

- `profile`: clock-profile index `0..4`, supported by the firmware's advertised
  `clock_profile_mask`. Profiles above `1` require explicit overclock acknowledgement.
- `address`: I2C address in `8..119`.
- `roles`: three integers for Banks A, B, and C; `0` means monitor and `1` generator.
- `acknowledge_overclock`: must be `true` when the selected profile requires it.

The client validates the draft before sending target settings and the final
save/reboot command. The example changes bank roles; it is not a read-only probe.
Already submitted configuration edits are not rolled back if a later step fails.
On failure, reread configuration and reconcile the target settings before retrying.

The lower-level configuration endpoints remain available with a board selection:

| Endpoint | JSON body |
| --- | --- |
| `POST /api/bank` | `{"bank": 0, "backend": 0, "role": 0}` |
| `POST /api/i2c-address` | `{"address": 64}` |
| `POST /api/clock-profile` | `{"profile": 0, "acknowledge_overclock": false}` |
| `POST /api/save` | `{"acknowledge_overclock": false}` |

Banks and backends use indexes `0..2`; fixed backends are hardware PWM (`0`),
PIO (`1`), and software (`2`). Keep the backend matched to the bank.
Configuration edits target the next startup; saving and rebooting are separate
steps unless using `/api/save-reboot`.

### Responses and Failures

`GET /api/host` returns `{"dashboard_type": "Python"}`. Status responses include
`connected`, `identity`, `configuration`, `runtime`, `pins`, and `channels`.
Inspect `error`, per-section error fields, and `bank_errors` as well as `connected`:
a connected board may still have unavailable telemetry or an invalid report.

Most successful actions return a refreshed status snapshot. Reboot and
save/reboot return `{"rebooting": true}` only after command confirmation; this
does not mean USB reconnection has finished.

| HTTP status | Meaning |
| --- | --- |
| `200` | Route completed; inspect the response for connection and section errors |
| `400` | Invalid request fields or a missing JSON object |
| `401` | Missing or incorrect authentication when a token is configured |
| `403` | Disallowed request host or cross-origin request |
| `409` | Unknown board selection, rejected command, protocol error, or unconfirmed outcome |
| `500` | Unexpected server failure; the body is `Internal server error` |
| `503` | Discovery or unhandled transport/runtime failure |

Route-level errors return a stable, user-safe `{"error": "description"}` message;
internal exception details are kept out of HTTP responses. Invalid request fields
return HTTP 400. A HID connection failure handled by `status()` can instead return
HTTP 200 with `connected: false` and a generic status error.
Likewise, an action may be confirmed before its refreshed status loses connection.
Both status reads and action snapshots replace exception messages in `error`,
per-section error fields, and non-null `bank_errors` entries with
`"Device status unavailable"`; diagnostic details are logged only on the server.
Do not treat HTTP success as proof that every response section is valid.

Do not blindly retry mutations after timeouts, disconnects, or unconfirmed
outcomes. Reconnect and inspect realized state or configuration first. An HTTP
client timeout does not cancel a command already submitted to the board.
Stop-all and other multi-channel updates apply one channel at a time while
firmware holds its control lock, and during that wait the board services USB
CDC only. Allow several seconds for those calls. `GET /api/boards` includes
each board's local `device_path` so the dashboard can reopen the HID node; an
authenticated client can see that path.

## Windows Installation Without Activation

If PowerShell blocks `Activate.ps1`, use the virtual environment directly.
Activation is optional; no execution-policy change is needed:

```powershell
py -m venv .venv
.\.venv\Scripts\python.exe -m pip install picopwm
.\.venv\Scripts\picopwm.exe
```

Open <http://127.0.0.1:5000> after starting the application.

## Development

From a repository checkout:

```sh
cd host/python
python -m pip install -e '.[test]'
python -m pytest -q
```

Alternatively, run `tools/host/run-python.sh` from the repository root to create a
virtual environment, refresh the editable installation, and start the dashboard.
The launcher uses `https://pypi.org/simple` by default; set `PIP_INDEX_URL` to use
another index. If `ensurepip` is unavailable, it can use the base interpreter's
pip with `--python`; otherwise install pip or the OS Python venv package.

Applications and tests can inject a client with `create_app(client)` from
`picopwm.app`.

Release wheels bundle the dashboard assets. Source checkouts serve the shared
assets from `host/web/` and `host/webhid/`; edit those canonical files rather than
generated or bundled copies.
