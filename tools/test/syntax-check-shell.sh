#!/usr/bin/env sh
set -eu

for script in \
    tools/firmware/build.sh \
    tools/firmware/load.sh \
    tools/firmware/setup-sdk-env.sh \
    tools/release/resolve-release-version.sh \
    tools/test/check.sh \
    tools/test/coverage-firmware-c.sh \
    tools/test/test-firmware-c.sh \
    tools/test/syntax-check-python.sh \
    tools/test/syntax-check-shell.sh; do
    sh -n "$script"
done
