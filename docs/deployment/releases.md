# Builds and Publishing

PicoPWM separates validation, release packaging and deployment, following PicoUart and PicoForge.
All workflows are available from the repository's [Actions tab](https://github.com/djwinter29-oss/PicoPWM/actions).

## What Runs When

| Workflow | Trigger | Result |
| --- | --- | --- |
| PR Check | Pull request, push to `main`, or manual dispatch | Formatting, firmware tests and coverage, `pico`/`pico2` builds, and Linux/macOS/Windows host tests. Firmware and coverage are downloadable workflow artifacts. |
| Release | Push a `vMAJOR.MINOR.PATCH` tag, or manual dispatch with a version | Validates and packages firmware, a Python wheel, and standalone PicoPWM (.NET) applications for Linux x64, Windows x64 and macOS ARM64. Only tag pushes create a GitHub Release. |
| Publish .NET Packages | Called by Release, or manual dispatch with a version | Produces downloadable archives and checksums. Direct manual runs mark the version as `X.Y.Z-test.RUN_ID.ATTEMPT`; release builds retain `X.Y.Z`. This does not publish to NuGet. |
| Publish Python to PyPI | Successful tag-triggered Release, or manual dispatch | Publishes the verified release wheel, or an explicitly requested development wheel. |
| Publish WebHID Pages | Successful tag-triggered Release, or manual dispatch | Deploys the released WebHID source, or an explicitly requested test deployment, to GitHub Pages. |

Build artifacts from PR Check and manual packaging runs are under **Actions > run > Artifacts**, not on the
Releases page. Publication does not happen on every push to `main`.

## Publish a Release

1. Merge the changes into `main` and wait for PR Check to succeed on that exact commit.
2. Push a new tag, for example `v1.2.3`, pointing to the tested commit.
3. Release builds both boards and packages PicoPWM for Python and .NET. Its validation jobs must succeed before the
   GitHub Release is published; the publication gate also requires successful PR checks for the same commit.
4. Publish Python to PyPI and Publish WebHID Pages run after Release succeeds. Each verifies the release tag and
   its successful checks before publishing.

For tag `v1.2.3`, the Python distribution version is `1.2.3`. Release packaging bundles the dashboard assets,
installs the wheel in a fresh environment, starts the installed `picopwm` command outside the source tree,
and verifies the dashboard and every bundled asset over localhost HTTP before publication.
After **Publish Python to PyPI** succeeds, users can run the following in an activated Python 3.10+ virtual environment:

```sh
python -m pip install picopwm==1.2.3
picopwm
```

Visit <http://localhost:5000/>. The page can load without a board; device control requires a connected PicoPWM board
and HID permissions. For future releases, use `python -m pip install --upgrade picopwm` to get the latest stable version.

Release versions come from the tag without the leading `v`. Major/minor must be 0-99 and patch 0-255. Local
`-dev` and `-rcN` firmware versions are not release tags. Manual Release runs build packages but do not publish a
GitHub Release or trigger automatic PyPI/Pages publication.

## Manual Test Publishing

These are real deployments, not dry runs. Choose the desired branch in **Run workflow**.

### Python

In **Publish Python to PyPI**, enable `test_publish` and leave `tag` empty. The workflow tests the Python host,
builds `0.0.0.devRUN_ID`, installs the wheel in a fresh environment and checks that its bundled dashboard loads.
It then publishes that development version to **production PyPI**, using the same `pypi` environment as a release.
Published versions are immutable: start a new workflow run to get a new version instead of rerunning a published one.

To publish an existing release instead, leave `test_publish` disabled and supply its tag, for example `v1.2.3`.
The workflow downloads and verifies the release wheel instead of rebuilding it.

### WebHID

In **Publish WebHID Pages**, enable `test_publish` and leave `tag` empty. The workflow checks the selected commit's
WebHID code, stamps `0.0.0-test.RUN_ID.ATTEMPT`, and deploys it to the **live Pages URL**, replacing the previous site.
This explicit test mode does not require a published release. Restore the release site by running again with
`test_publish` disabled and the release tag supplied.

### .NET

Run **Publish .NET Packages** with a base version such as `1.2.3`. The marked standalone packages are uploaded as
workflow artifacts for all three platforms; this does not create a GitHub Release.
The application is named **PicoPWM**; archive filenames retain `picopwm-host-v<VERSION>-<RID>`
with `.zip` for Windows and `.tar.gz` for Linux and macOS.

## One-Time Repository Setup

Workflow files alone cannot configure external publishing services:

- Enable GitHub Actions for the repository and allow the actions referenced by these workflows.
- Set **Settings > Pages > Build and deployment > Source** to **GitHub Actions**. The workflow uses the
  `github-pages` deployment environment.
- Configure a PyPI Trusted Publisher for project `picopwm`, owner `djwinter29-oss`, repository `PicoPWM`,
  workflow `publish-pypi.yml`, and environment `pypi`. Create the corresponding GitHub environment and configure
  any required approval rules. No PyPI API-token secret is needed.
- Ensure deployment-environment branch/tag policies allow the intended release tags and any branches used for
  manual test publishing. Required approvals can leave publication waiting even after packaging succeeds.

If publication does not start, check the triggering Release run first: a queued, failed, cancelled or manually
dispatched Release does not trigger automatic publication. A successful build also does not qualify hardware;
record board-level testing separately.
