# PicoPWM Direct Dashboard

Control and monitor 24 PWM channels directly from your browser over USB HID. No Python or .NET host service is needed.

[Open Direct Dashboard](https://djwinter29-oss.github.io/PicoPWM/) ·
[Firmware and wiring](../../README.md) · [HID reference](../../docs/design/usb/hid-report-reference.md)

## Requirements

- Desktop Chrome or Edge, using HTTPS or a loopback address. Firefox, Safari, and mobile browsers are not supported.
- A Pico or Pico 2 running compatible PicoPWM firmware, connected to the browser's computer by a USB data cable.
- Browser device permission and OS access; Linux needs [HID permissions setup](../../docs/guides/setup-and-troubleshooting.md#linux-hid-access).

## Quick Start

1. Connect the board, open the dashboard, and click **Connect**.
2. Select the PicoPWM HID interface and grant permission.
3. Check device identity and running bank roles. Banks default to monitor-only.
4. If the intended bank is currently a monitor, select Generator under **Boot configuration**, then review and
   confirm **Save & Reboot**. Reconnect and verify the applied settings. Skip this step if it is already a generator.
5. Click a generator's underlined frequency or duty, edit the values, and click **Apply**.
   **This changes live output immediately; verify the GPIO and wiring first.**

Use one controlling host per board. Status refreshes every second; reconnection is manual.

## Controls and Readings

| Control | Behavior |
| --- | --- |
| PWM tables | Hardware channels 0-7, PIO 8-15, software 16-23. Backends are fixed; roles apply to whole banks. Monitors are read-only. |
| Channel editor | Starts from realized output. Hz/kHz/MHz conversion preserves frequency. The 1 kHz / 50% preset only fills fields; Cancel writes nothing. |
| Boot configuration | Clock profile, decimal I2C address (8-119), and bank roles. Edits remain local until confirmed. |
| Pin assignment | Expand for GPIO IDs, not physical header-pin numbers. |
| Board LED / Reboot Board | Toggle the LED or confirm a restart without saving edits. |

**Unsaved edits** marks local changes. **Target differs from running** does not prove settings are saved or
identify clock-profile differences. Missing readings show explicit errors; unsupported features require newer firmware.

Readback is not an independent electrical measurement. RAM is static reservation, temperature is an uncalibrated
internal estimate, and service load is not CPU utilization. See the [HID reference](../../docs/design/usb/hid-report-reference.md)
for interpretation and the [frequency guidance](../../README.md#recommended-frequency-ranges) for output limitations.

## Saving Safely

- Stop software generators before saving: flash writes briefly pause HID, CDC, and I2C and disturb software output.
- Save & Reboot requires acknowledgement for every explicit clock profile, including 150 MHz.
  SDK default needs no checkbox; 150 MHz overclocks RP2040 but is the default speed on RP2350.
  Saving does not establish stability. For an unbootable profile, follow the
  [BOOTSEL and flash-reset recovery guide](../../docs/guides/setup-and-troubleshooting.md#recover-an-unbootable-configuration-or-reset-flash).
- A failed settings command prevents the final save but may leave partial target changes.
  After a failed or unconfirmed operation, reconnect and inspect target settings before retrying.

## Troubleshooting

- **Cannot connect:** check the browser, secure context, USB data cable, firmware, and device permissions.
  Close competing HID clients. USB access is local to the browser's computer, even with a remote workspace.
- **Save & Reboot disabled:** check the button's explanation. There may be no pending changes, or firmware may
  lack the capability. Flashing firmware updates the board; building alone does not.

## Development

From the repository root, serve locally and run tests in separate terminals:

```sh
tools/host/run-direct.sh
node --test host/direct/tests/*.test.mjs host/direct/tests/js/*.test.mjs
```

The launcher needs Python 3.10+, with no extra packages, and serves `http://127.0.0.1:8000/` (`--port` to override).
Use `--port 0` to let the OS choose an available port; the startup message prints the bound URL.
Loopback startup does not require reverse DNS. The server lifecycle test captures a child stack dump
if startup stalls, so CI failures identify the blocking operation.
It serves a temporary staged copy of `src/`, cleaned up when stopped. Restart after editing source to rebuild
the copy. Canonical `src/` remains unchanged and also supplies shared assets to Python/.NET hosts.
`tests/` contains hardware-free checks, not physical PWM qualification.
Tests mirror the source layout and names:

| Source | Test |
|---|---|
| `src/index.html` and its asset references | `tests/index.test.mjs` |
| `src/js/app.mjs` | `tests/js/app.test.mjs` (browser flow) |
| `src/js/device.mjs` | `tests/js/device.test.mjs` |
| `src/js/view.mjs` | `tests/js/view.test.mjs` |

Shared fake devices and browser setup live in `tests/fixtures/`. The command includes both page and
JavaScript tests; pass one test file to run only its corresponding source checks.
Asset/selector assertions intentionally follow current HTML and module structure; shared hosted-dashboard
tests also extract Direct event-handler source into a VM. Update these checks with markup/module refactors.
They supplement device/view behavior tests, but do not execute a full browser or qualify WebHID.

The browser regression additionally runs the actual HTML/modules in headless Chromium with a fake HID
device. It covers connection, channel editing and readback, dialog focus, save acknowledgement/confirmation/cancellation,
and timeout/disconnect/reconnect. It uses a temporary local-file copy with file-module access enabled;
it does not qualify HTTPS/Pages delivery, the native device chooser, or physical USB/PWM behavior.
The runner waits for the fixture's explicit PASS/FAIL result over Chromium's debugging pipe using real
time, not a virtual-time DOM snapshot. Failures include fixture progress, browser exceptions and console
diagnostics. No npm dependencies are required. The same Node test command discovers it automatically:

```sh
CHROMIUM=/usr/bin/chromium node --test host/direct/tests/*.test.mjs host/direct/tests/js/*.test.mjs
```

Without `CHROMIUM`, it tries `chromium` and explicitly skips if absent. Setting `CHROMIUM` makes a missing
executable a failure; Linux host CI requires the runner's installed Google Chrome. Native sandboxing stays
enabled unless an isolated test environment explicitly sets `CHROMIUM_NO_SANDBOX=1`.

Commands reserve the client through all writes and readback, including the complete Save & Reboot
sequence and INVALID reconciliation. A user command waits for an active background refresh, then
rechecks the connection before writing; competing commands are rejected. Background refresh never
interleaves with a command. Save & Reboot snapshots roles before validating the draft.
Firmware failures expose numeric `DeviceError.status`; wrapped failures retain their `cause`.
The selected HID collection must match both vendor usage page `0xff00` and usage `0x01`.

A HID transfer or command-result deadline leaves the connection blocked with a visible reconnect-required error. Commands
stay disabled, while Disconnect remains available. Disconnect and reconnect before retrying: WebHID
cannot cancel the outstanding transfer, and a timed-out write may have taken effect.

Native device open/close waits are bounded to three seconds (the device chooser is not timed out).
If opening times out or closing fails, the page displays **Recovery required** with instructions to unplug
the board and reload before reconnecting. Native work is not canceled: a late successful open is closed,
and late completion cannot restore the timed-out connection. Cleanup failures are reported rather than
silently ignored.

### Dashboard version and cache identity

Pages release builds display the release version, for example `1.2.3`; explicit test deployments display
`0.0.0-test.RUN_ID.ATTEMPT`. Hover over the version for the source commit. The staged `build.json` also
records the full commit, dirty-worktree flag, and cache key. Firmware version remains independent.

Local launcher versions come from Git: `v1.2.3` produces `1.2.3+g<commit>`, one later commit produces
`1.2.4-dev.1+g<commit>`, and uncommitted changes add `.dirty`. With no version tags, the version is
`0.0.0-dev.<commit-count>+g<commit>`; without Git metadata the launcher explicitly warns and uses
`0.0.0-debug` with an unknown commit. Open a full checkout with tags for meaningful development versions.

The staging tool hashes the source assets and build identity once, then applies the same generated `v`
query to HTML assets and their transitive module/CSS references. Any asset change invalidates the whole
graph, avoiding mismatched module cache versions. Source query values remain for raw-source use; staged
sites do not rely on those manually maintained values. Local serving disables caching.
Pages HTML revalidation remains governed by GitHub Pages; an already-open page does not auto-update.

CI stages into `build/direct-site`, verifies the final version/commit label and reference graph after stamping,
then uploads only that directory. To build and verify manually, choose a new output directory:

```sh
python3 tools/host/direct_site.py build --output build/direct-site --version 1.2.3
python3 tools/host/direct_site.py verify --output build/direct-site --version 1.2.3
python3 tools/test/test_direct_site.py
```

Existing output directories are rejected rather than overwritten. These staging checks do not replace
browser rendering or hardware validation.
