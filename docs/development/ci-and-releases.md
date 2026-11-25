# CI and Releases

Builds and publishing run through [GitHub Actions](https://github.com/djwinter29-oss/PicoPWM/actions).

- [Workflow triggers](#what-runs-when)
- [Versioning](#versioning)
- [Publish a release](#publish-a-release)
- [Manual test publishing](#manual-test-publishing)
- [One-time repository setup](#one-time-repository-setup)

## What Runs When

| Workflow | Trigger | Result |
| --- | --- | --- |
| PR Check | Pull request, push to `main`, or manual dispatch | Formatting, firmware tests/coverage, both board builds, and Linux/macOS/Windows host tests. Firmware and coverage are artifacts. |
| Release | Push a `vMAJOR.MINOR.PATCH` tag, or manual dispatch with a version | Validates and packages both firmwares, a Python wheel, and .NET apps for Linux x64, Windows x64, and macOS ARM64. Only tags create a GitHub Release. |
| Publish .NET Packages | Called by Release, or manual dispatch with a version | Produces downloadable archives and checksums. Direct manual runs mark the version as `X.Y.Z-test.RUN_ID.ATTEMPT`; release builds retain `X.Y.Z`. This does not publish to NuGet. |
| Publish Python to PyPI | Successful tag-triggered Release, or manual dispatch | Publishes the verified release wheel, or an explicitly requested development wheel. |
| Publish Direct Dashboard Pages | Successful tag-triggered Release, or manual dispatch | Deploys the released Direct Dashboard source, or an explicitly requested test deployment, to GitHub Pages. |

PR and manual-build artifacts are under **Actions > run > Artifacts**, not
Releases. A push to `main` does not publish packages.

## Versioning

Release versions come from `vMAJOR.MINOR.PATCH` tags, without `v`.
Local/manual version sources:

| Artifact | Local version source | Manual build override |
| --- | --- | --- |
| Firmware | `--firmware-version`; default `0.0.0-dev` | `--firmware-version <version>` |
| Python wheel | Git tag/history via Hatchling and hatch-vcs; dirty checkouts include a local marker | `SETUPTOOLS_SCM_PRETEND_VERSION` |
| .NET app | MinVer: `v1.2.3` builds `1.2.3`; next commit builds `1.2.4-dev.1`; untagged history falls back to `0.0.0-dev` | `-p:MinVerVersionOverride=<version>` |
| Direct Dashboard | Git tags and commit height; dirty checkouts add `.dirty` | `--version <version>` for staged builds |

Keep Git history and tags when building host packages locally. Check .NET
version behavior with `python3 tools/test/test_dotnet_version.py`.

## Publish a Release

1. Merge the changes into `main` and wait for PR Check to succeed on that exact commit.
2. Push a new tag, for example `v1.2.3`, pointing to the tested commit.
3. Release validates both firmwares and host packages, including the Python
   wheel. Publication also requires successful PR checks for the same commit.
4. After Release succeeds, PyPI and Pages workflows verify its tag and checks,
   then publish.

Major/minor release components must be 0-99 and patch 0-255. Local `-dev` and
`-rcN` firmware versions are not release tags. Manual Release runs build
packages but do not publish a GitHub Release or trigger automatic PyPI/Pages
publication.

## Manual Test Publishing

**These publish to live services.** Select the branch in **Run workflow**.

### Python

In **Publish Python to PyPI**, enable `test_publish` and leave `tag` empty.
It tests the host, builds `0.0.0.devRUN_ID`, installs the wheel in a fresh
environment, and checks its dashboard. It publishes to **production PyPI**
through the release `pypi` environment. Versions are immutable: start a new
run, not a rerun, after publication.

To publish an existing release instead, leave `test_publish` disabled and supply its tag, for example `v1.2.3`.
The workflow downloads and verifies the release wheel instead of rebuilding it.

### Direct Dashboard

In **Publish Direct Dashboard Pages** (`publish-direct.yml`), enable `test_publish` and leave `tag` empty.
The workflow checks the selected commit's Direct Dashboard code, stamps `0.0.0-test.RUN_ID.ATTEMPT`, and deploys it
to the **live Pages URL**, replacing the previous site.
This explicit test mode does not require a published release. Restore the release site by running again with
`test_publish` disabled and the release tag supplied.

Before upload, staging verifies the stamped version and local references
without editing source. Already-open pages do not update.

### .NET

Run **Publish .NET Packages** with a base version such as `1.2.3`. The marked standalone packages are uploaded as
workflow artifacts for all three platforms; this does not create a GitHub Release.
The application is named **PicoPWM**; archive filenames retain `picopwm-host-v<VERSION>-<RID>`
with `.zip` for Windows and `.tar.gz` for Linux and macOS.

Each platform's smoke check launches the published executable and verifies
version, dashboard/asset bytes, local references, and graceful zero-exit
shutdown. Failure prevents archiving/upload. It uses no hardware and does
not qualify browser/HID behavior. Run locally with:

```sh
python3 tools/release/check_dotnet_publish.py --directory build/host-release --version 1.2.3
```

## One-Time Repository Setup

- Enable GitHub Actions for the repository and allow the actions referenced by these workflows.
- Set **Settings > Pages > Build and deployment > Source** to **GitHub Actions**. The workflow uses the
  `github-pages` deployment environment.
- Configure a PyPI Trusted Publisher for project `picopwm`, owner `djwinter29-oss`, repository `PicoPWM`,
  workflow `publish-pypi.yml`, and environment `pypi`. Create the corresponding GitHub environment and configure
  any required approval rules. No PyPI API-token secret is needed.
- Ensure deployment-environment branch/tag policies allow the intended release tags and any branches used for
  manual test publishing. Required approvals can leave publication waiting even after packaging succeeds.

If publication does not start, check Release first: queued, failed, cancelled,
and manual runs do not trigger automatic publishing. Record hardware tests
separately; successful builds do not qualify a board.
