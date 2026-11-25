# Host App Development

This page covers host-app development, automated tests, and packaging checks.
For installation and normal use, start with the
[host application guides](../README.md#use-picopwm). These checks do not
qualify physical PWM output, monitor accuracy, or native USB behavior. See
[Native HID Qualification](../validation/native-hid-qualification.md) for platform-specific
device checks.

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
Chromium flow with a fake HID device. The flow covers connection, editing,
readback, save confirmation, and timeout/disconnect recovery. It does not test
the native device chooser, real WebHID, Pages delivery, or physical USB/PWM.
Without `CHROMIUM`, the browser test tries `chromium` and skips if unavailable;
set the variable to require the browser check:

```sh
CHROMIUM=/usr/bin/chromium node --test host/direct/tests/*.test.mjs host/direct/tests/js/*.test.mjs
```

Source/selector assertions follow the current HTML and module structure.
Update them when refactoring markup or modules; they complement behavior tests
but are not a full browser or device qualification.

WebHID cannot cancel an outstanding transfer. After a timeout, the page
requires disconnect/reconnect before another command; an uncertain write may
have taken effect. Native device open/close waits are bounded to three seconds
(the browser chooser is not timed out). A failed or timed-out open/close
requires unplugging the board and reloading before reconnecting.

To stage and verify a local Pages build, use a new output directory:

```sh
python3 tools/host/direct_site.py build --output build/direct-site --version 1.2.3
python3 tools/host/direct_site.py verify --output build/direct-site --version 1.2.3
python3 tools/test/test_direct_site.py
```

The tool rejects an existing output directory. It stamps the build identity
and cache key across HTML, modules, and CSS, then verifies local references.
Release builds show the release version; explicit test builds use
`0.0.0-test.RUN_ID.ATTEMPT`. Local launcher versions come from Git tags and
commit height: a later commit after `v1.2.3` is `1.2.4-dev.1`, dirty
checkouts add `.dirty`, and missing Git metadata produces an explicit warning
and debug version. Pages deployments stamp a shared cache key through local
HTML, module, and CSS references; they do not update an already-open page.
These checks do not replace browser rendering or hardware validation.

## Shared Python and .NET Dashboard

The browser client tests use a simulated HTTP API:

```sh
node --test host/web/tests/*.test.mjs
```

They cover login, board selection, editing, save acknowledgement, and
uncertain-write recovery. The Chromium flow runs from a temporary local-file
copy; it does not qualify Flask or ASP.NET delivery, authentication cookies,
or physical USB/PWM. Set `CHROMIUM` to require the browser; otherwise the test
explicitly skips if Chromium is unavailable. Linux host CI requires it.

Reads and writes have bounded HTTP waits, but canceling an HTTP request does
not cancel USB work on the host. A mutation with an uncertain result is not
automatically replayed; fetch fresh status before retrying.

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
tests mirror those areas. Some concurrency and shared-dashboard checks inspect
private implementation details; update them with synchronization or markup
refactors. They do not define a public locking API or prove native hidapi
transfer deadlines.

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

The shared web client tests are listed above. They run in a local-file browser
copy and do not qualify real Flask/ASP.NET delivery, authentication cookies,
or physical USB/PWM. The HTTP API's timeout and uncertain-write behavior is
described in the [Dashboard HTTP API](../protocols/dashboard-http-api.md).

## .NET Host

Install the .NET 10 SDK and run the solution tests from the repository root:

```sh
dotnet test host/dotnet/PicoPwm.sln --configuration Release --nologo
```

The application uses MinVer and Git tags for versioning. A `v1.2.3` tag builds
as `1.2.3`; the next commit builds as `1.2.4-dev.1`. Dirty-worktree changes do
not affect the version, and an untagged history falls back to `0.0.0-dev`.
Use a full checkout with tags. For a manual version override, use
`-p:MinVerVersionOverride=<version>` rather than `-p:Version`; the regression
check is `python3 tools/test/test_dotnet_version.py`.

The code is grouped under `PicoPwm/Dashboard/`, `Device/`, and `Hid/`;
`Program.cs` is the entry point, with matching tests and shared fakes under
`PicoPwm.Tests/`. Transport tests use in-memory HidSharp device/stream
subclasses to cover discovery, framing, writes, timeouts, and disposal. Lock
regressions inspect private synchronization state; they are implementation
checks, not a public lock API or proof that native calls cannot stall.

After publishing, the packaging smoke check can be run locally:

```sh
python3 tools/release/check_dotnet_publish.py --directory build/host-release --version 1.2.3
```

It launches the published executable, checks `/api/host`, version and bundled
assets/references over loopback HTTP, then requires graceful zero-exit
shutdown. Startup failures include captured logs. It does not access a board
or qualify browser rendering or native HID behavior.

## Platform Qualification

Unit tests do not establish native USB transfer deadlines or physical
qualification. Follow [Native HID Qualification](../validation/native-hid-qualification.md)
for platform-specific device testing and reporting.
