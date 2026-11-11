# Security

To report a suspected vulnerability, see [Report a Vulnerability](#report-a-vulnerability).

## Trust Model

PicoPWM is a locally controlled hardware tool, not a locked-down appliance. It trusts users with physical access
to the board and applications authorized to access its interfaces.
Trust in a physical operator does not extend to every website or application on their computer.

- The USB CDC shell, USB HID interface, and I2C slave intentionally have no firmware-level authentication.
  PWM control and configuration changes through these interfaces are intended functionality.
- Physical access permits reflashing through BOOTSEL. Protection against a physical operator replacing the firmware
  is outside the project's security model.
- Access is controlled through operating-system device permissions, browser device grants, dashboard authentication
  where configured, and physical control of the I2C bus. Grant access only to trusted users and applications.

## Vulnerability Reporting Scope

Report defects that violate the trust model, including:

- Device control by an untrusted website or remote client without authorization.
- Bypasses of configured dashboard authentication or other access controls.
- Memory corruption caused by malformed input.

These are examples of reportable defects, not a list of known vulnerabilities. Authorized shell and dashboard
access, device selection, and configuration changes are not vulnerabilities by themselves.

For host setup and deployment guidance, see the [Python host guide](host/python/README.md),
[.NET host guide](host/dotnet/README.md), and [Direct Dashboard guide](host/direct/README.md).

## Supported Versions

Security fixes target only the firmware and Python, .NET, and Direct Dashboard host applications associated with the
[latest stable PicoPWM release](https://github.com/djwinter29-oss/PicoPWM/releases/latest).
Older releases and development or prerelease builds are not supported for security fixes.
Users of those versions should upgrade to the latest stable release.

## Report a Vulnerability

Report suspected vulnerabilities privately through the repository's **Security** tab using **Report a
vulnerability**. Do not publish exploit details in an issue or pull request.

Include the following in your private report:

- Affected firmware or host application version, board model, and relevant environment details.
- Steps to reproduce the issue, with a minimal example where possible.
- Expected and observed behavior, security impact, and any access required to reproduce it.

Do not include credentials, tokens, or other sensitive information.

If private reporting is unavailable, open a minimal issue asking maintainers for a private reporting channel; do not
include technical details until one is provided.

Maintainers triage reports privately and may request additional information to reproduce and assess the issue.
For confirmed vulnerabilities, maintainers coordinate fixes and public disclosure with the reporter.
Response and resolution times depend on maintainer availability and the issue's complexity; no fixed timeline
is guaranteed. Please coordinate disclosure before publishing exploit details.

## Operational Safety

Authorized control can still produce unsafe outputs or settings. Wiring mistakes, unsuitable PWM settings, and
intentional overclocking are operational risks, not security vulnerabilities by themselves. Follow the
[hardware setup guide](docs/guides/hardware-setup.md).

Saving a configuration writes flash and can select an overclocked system clock. A saved profile that prevents boot
can be recovered by holding BOOTSEL while connecting USB and reflashing.
