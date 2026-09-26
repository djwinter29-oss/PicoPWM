#!/usr/bin/env sh
set -eu

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname "$0")" && pwd)
REPO_ROOT=$(CDPATH= cd -- "$SCRIPT_DIR/../.." && pwd)
BUILD_DIR=$(mktemp -d "${TMPDIR:-/tmp}/picopwm-cli-test.XXXXXX")
trap 'rm -rf "$BUILD_DIR"' EXIT

CC_VALUE="${CC:-cc}"

"$CC_VALUE" \
    -std=c11 \
    -Wall \
    -Wextra \
    -Werror \
    -Wno-sign-compare \
    -I"$REPO_ROOT/firmware/src" \
    -I"$REPO_ROOT/firmware/third_party/microrl" \
    "$REPO_ROOT/test/cli_shell_test.c" \
    "$REPO_ROOT/firmware/src/cli/shell.c" \
    "$REPO_ROOT/firmware/third_party/microrl/microrl.c" \
    -o "$BUILD_DIR/cli_shell_test"

"$BUILD_DIR/cli_shell_test"

"$CC_VALUE" \
    -std=c11 \
    -Wall \
    -Wextra \
    -Werror \
    -I"$REPO_ROOT/test/stubs" \
    -I"$REPO_ROOT/firmware/src" \
    -I"$REPO_ROOT/firmware/third_party/microrl" \
    "$REPO_ROOT/test/cli_channel_commands_test.c" \
    "$REPO_ROOT/firmware/src/cli/pwm_channel_commands.c" \
    -o "$BUILD_DIR/cli_channel_commands_test"

"$BUILD_DIR/cli_channel_commands_test"
