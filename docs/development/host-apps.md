# Host App Development

Local builds, tests, and site staging. Commands run from the repository root
unless stated otherwise. Install only the needed toolchain:

- [Direct Dashboard](#direct-dashboard) — Python 3.10+ to serve, Node.js to test
- [Shared browser assets](#shared-python-and-net-dashboard) — Node.js
- [Python host](#python-host) — Python 3.10+ and the test extra
- [.NET host](#net-host) — .NET 10 SDK
- [Launcher checks](#host-launcher-checks) — Python

For releases, see [CI and Releases](ci-and-releases.md).
Installation: [browser](../../host/direct/README.md),
[Python](../../host/python/README.md), [.NET](../../host/dotnet/README.md).

## Direct Dashboard

To run the dashboard locally, start the server:

```sh
tools/host/run-direct.sh
```

The Python 3.10+ launcher needs no extra packages. It serves a staged copy of
`host/direct/src/` at `http://127.0.0.1:8000/`; restart after editing source.
Use `--port 0` for an available port. These sources also supply shared assets
to the Python/.NET hosts.

Run the tests independently; no local server is required:

```sh
node --test host/direct/tests/*.test.mjs host/direct/tests/js/*.test.mjs
```

Node tests check structure, JavaScript, and a fake-HID Chromium flow.
The browser test tries `chromium` and skips if absent; set `CHROMIUM` to
require it:

```sh
CHROMIUM=/usr/bin/chromium node --test host/direct/tests/*.test.mjs host/direct/tests/js/*.test.mjs
```

For a staged site build and verification:

```sh
python3 tools/host/direct_site.py build --output build/direct-site --version 1.2.3
python3 tools/host/direct_site.py verify --output build/direct-site --version 1.2.3
python3 tools/test/test_direct_site.py
```

The output directory must be new. Verification checks identity, cache keys,
and local references without editing source. Local versions use Git tags
and commit height; dirty checkouts add `.dirty`. Missing Git metadata warns
and uses a debug version.

## Shared Python and .NET Dashboard

Browser tests simulate the shared [backend API](dashboard-api.md):

```sh
node --test host/web/tests/*.test.mjs
```

They cover login, board selection, editing, save acknowledgement, and
uncertain writes. Chromium runs a temporary local-file copy; set `CHROMIUM`
to require it. Otherwise it skips if absent. Linux host CI requires Chromium.

## Python Host

From `host/python/`, install the test extra and run the package tests:

```sh
python -m pip install -e '.[test]'
python -m pytest -q
```

`tools/host/run-python.sh` creates a venv, refreshes the editable install,
and starts the dashboard. The default index is `https://pypi.org/simple`;
override with `PIP_INDEX_URL`. Without `ensurepip`, it may use base pip with
`--python`; if neither is available, install pip or the OS venv package.

Sources and tests are grouped by `dashboard/` (HTTP), `device/` (clients and
registry), and `hid/` (transport/codecs) under `picopwm/`.

When embedding `create_app()`, close the registry after requests and workers
stop, including exceptional shutdown:

```python
from picopwm.dashboard.app import create_app

app = create_app()
try:
    app.run(host="127.0.0.1", port=5000)
finally:
    app.extensions["picopwm_boards"].close()
```

With WSGI, close during worker shutdown, not request teardown. Each worker
owns its registry and locks; use one worker per board. Injected clients remain
caller-owned. Closing handles does not stop outputs.

## .NET Host

Install the .NET 10 SDK. For an optional local run, start the application
from the repository root:

```sh
dotnet run --project host/dotnet/PicoPwm
```

This remains in the foreground until stopped. To run tests, without starting
the application:

```sh
dotnet test host/dotnet/PicoPwm.sln --configuration Release --nologo
```

Sources are under `PicoPwm/Dashboard/`, `Device/`, and `Hid/`, with
`Program.cs` as entry point and tests in `PicoPwm.Tests/`. Transport tests use
in-memory HidSharp fakes for discovery, framing, writes, timeouts, and disposal.

`tools/host/run-dotnet.sh` restores, builds, and starts the app. A newer SDK
still needs the .NET 10 ASP.NET Core runtime. Edit `host/web/src/` and
`host/direct/src/`, not generated `wwwroot`.

## Host Launcher Checks

Launcher regression tests:

```sh
python tools/test/test_run_python.py
python tools/test/test_run_dotnet.py
```

## Platform Qualification

Fakes do not qualify native USB timing or physical PWM.
Run [native HID checks](../validation/native-hid-qualification.md) and
[hardware tests](../validation/hil/test-plan.md) separately.
