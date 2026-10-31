#!/usr/bin/env sh
# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026 Ji Dong

set -eu

for script in \
    tools/host/remove-local-branches.sh \
    tools/host/run-dotnet.sh \
    tools/host/run-hil.sh \
    tools/host/run-python.sh \
    tools/host/run-webhid.sh \
    tools/host/hid-permissions-check.sh \
    tools/host/hid-permissions-setup.sh \
    tools/firmware/build.sh \
    tools/firmware/load.sh \
    tools/release/resolve-release-version.sh \
    tools/test/check-firmware-invariants.sh \
    tools/test/check.sh \
    tools/test/coverage-firmware-c.sh \
    tools/test/test-firmware-c.sh \
    tools/test/test-host.sh \
    tools/test/syntax-check-python.sh \
    tools/test/syntax-check-shell.sh; do
    sh -n "$script"
done
