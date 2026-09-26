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

"$CC_VALUE" \
    -std=c11 \
    -Wall \
    -Wextra \
    -Werror \
    -I"$REPO_ROOT/test/stubs" \
    -I"$REPO_ROOT/firmware/src" \
    "$REPO_ROOT/test/pwm_profile_test.c" \
    "$REPO_ROOT/firmware/src/profile/pwm_profile.c" \
    "$REPO_ROOT/firmware/src/profile/profiles/profile_111.c" \
    -o "$BUILD_DIR/pwm_profile_test"

"$BUILD_DIR/pwm_profile_test"

"$CC_VALUE" \
    -std=c11 \
    -Wall \
    -Wextra \
    -Werror \
    -DPICO_PWM_MONITOR_PROFILE=1 \
    -I"$REPO_ROOT/test/stubs" \
    -I"$REPO_ROOT/firmware/src" \
    "$REPO_ROOT/test/pwm_profile_test.c" \
    "$REPO_ROOT/firmware/src/profile/pwm_profile.c" \
    "$REPO_ROOT/firmware/src/profile/profiles/profile_222.c" \
    -o "$BUILD_DIR/pwm_profile_monitor_test"

"$BUILD_DIR/pwm_profile_monitor_test"

for code in 112 121 122 211 212 221; do
    "$CC_VALUE" \
        -std=c11 \
        -Wall \
        -Wextra \
        -Werror \
        -I"$REPO_ROOT/test/stubs" \
        -I"$REPO_ROOT/firmware/src" \
        "$REPO_ROOT/test/pwm_profile_test.c" \
        "$REPO_ROOT/firmware/src/profile/pwm_profile.c" \
        "$REPO_ROOT/firmware/src/profile/profiles/profile_$code.c" \
        -o "$BUILD_DIR/pwm_profile_${code}_test"

    "$BUILD_DIR/pwm_profile_${code}_test"
done
