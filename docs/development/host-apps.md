# Host App Development

This page covers host-app development and automated tests. For deployment,
versioning, packaging, and site-staging instructions, see
[CI and Releases](ci-and-releases.md). For installation and normal use, start
with the [host application guides](../README.md#use-picopwm).

## Direct Dashboard

From the repository root, use separate terminals to serve the dashboard and
run its tests:

```sh
tools/host/run-direct.sh
node --test host/direct/tests/*.test.mjs host/direct/tests/js/*.test.mjs
```

The launcher needs Python 3.10+ and no extra packages. It serves a temporary
staged copy of `host/direct/src/` at `http://127.0.0.1:8000/`; restart after
editing source to rebuild the copy. Use `--port 0` to choose an available port.
The canonical files also supply shared assets to the Python and .NET hosts.

The Node tests cover page structure, JavaScript behavior, and a headless
Chromium flow with a fake HID device. Without `CHROMIUM`, the browser test
tries `chromium` and skips if unavailable; set the variable to require it:

```sh
CHROMIUM=/usr/bin/chromium node --test host/direct/tests/*.test.mjs host/direct/tests/js/*.test.mjs
```

For a staged site build and verification:

```sh
python3 tools/host/direct_site.py build --output build/direct-site --version 1.2.3
python3 tools/host/direct_site.py verify --output build/direct-site --version 1.2.3
python3 tools/test/test_direct_site.py
```

Use a new output directory; the tool rejects an existing one. It verifies
build identity, cache keys, and local references without modifying source.
`run-direct.sh` derives a local version from Git tags and commit height; dirty
checkouts add `.dirty`, and missing Git metadata produces a warning and debug
version. See [CI and Releases](ci-and-releases.md) for deployment behavior.

## Shared Python and .NET Dashboard

The browser client tests use a simulated HTTP API:

```sh
node --test host/web/tests/*.test.mjs
```

They cover login, board selection, editing, save acknowledgement, and
uncertain-write recovery. The Chromium flow runs from a
temporary local-file copy. Set `CHROMIUM` to require it; otherwise it skips
when Chromium is unavailable. Linux host CI requires it.

## Python Host

From `host/python/`, install the test extra and run the package tests:

```sh
python -m pip install -e '.[test]'
python -m pytest -q
```

The `tools/host/run-python.sh` launcher creates a virtual environment, refreshes
the editable installation, and starts the dashboard. It uses
`https://pypi.org/simple` by default; set `PIP_INDEX_URL` to use another index.
If `ensurepip` is unavailable, it may use the base interpreter's pip with
`--python`; otherwise install pip or the OS Python venv package.

Package code is grouped under `picopwm/dashboard/` (HTTP app), `device/`
(client and board registry), and `hid/` (transport and report codecs). The
tests mirror those areas.

When embedding `create_app()`, the application owner closes the board registry
after requests and worker activity have stopped, including on exceptional
shutdown:

```python
from picopwm.dashboard.app import create_app

app = create_app()
try:
    app.run(host="127.0.0.1", port=5000)
finally:
    app.extensions["picopwm_boards"].close()
```

For an externally managed WSGI server, close it during worker shutdown, not
per-request teardown. Each worker owns a registry and client locks; use one
worker process per board. An injected client remains caller-owned and must be
closed separately. Closing handles does not stop generator outputs.

The shared web client tests are listed above. HTTP timeout and uncertain-write
behavior is described in the [Dashboard HTTP API](../protocols/dashboard-http-api.md).

## .NET Host

Install the .NET 10 SDK. From the repository root, run the application and its
tests with:

```sh
dotnet run --project host/dotnet/PicoPwm
dotnet test host/dotnet/PicoPwm.sln --configuration Release --nologo
```

The code is grouped under `PicoPwm/Dashboard/`, `Device/`, and `Hid/`;
`Program.cs` is the entry point, with matching tests and shared fakes under
`PicoPwm.Tests/`. Transport tests use in-memory HidSharp device/stream
subclasses to cover discovery, framing, writes, timeouts, and disposal.

The POSIX launcher `tools/host/run-dotnet.sh` also restores, builds, and starts
the application. With a newer SDK, install the .NET 10 ASP.NET Core runtime.
Edit canonical assets in `host/web/src/` and `host/direct/src/`, not generated
`wwwroot` output.

## Host Launcher Checks

The launchers have focused regression tests:

```sh
python tools/test/test_run_python.py
python tools/test/test_run_dotnet.py
```

## Platform Qualification

Automated tests do not establish native USB transfer deadlines or physical PWM
qualification. Follow [Native HID Qualification](../validation/native-hid-qualification.md)
for platform-specific device checks, and the [HIL test plan](../validation/hil/test-plan.md)
for physical PWM testing.
