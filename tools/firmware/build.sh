#!/usr/bin/env sh
# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026 Ji Dong

# Usage: tools/firmware/build.sh [--board pico|pico2] [--firmware-version VERSION]
# Builds into build/<board>. Fresh builds prefer Ninja; CMAKE_GENERATOR overrides the default.
set -eu

BOARD="${PICO_BOARD:-pico}"
FIRMWARE_VERSION="${PICO_PWM_FIRMWARE_VERSION:-}"

while [ "$#" -gt 0 ]; do
    case "$1" in
        --board) BOARD="$2"; shift 2 ;;
        --firmware-version) FIRMWARE_VERSION="$2"; shift 2 ;;
        *) echo "Unknown argument: $1" >&2; exit 1 ;;
    esac
done

case "$BOARD" in
    pico|pico2) ;;
    *) echo "Unsupported board '$BOARD'. Use 'pico' or 'pico2'." >&2; exit 1 ;;
esac

REPO_ROOT=$(CDPATH= cd -- "$(dirname "$0")/../.." && pwd)
BUILD_DIR="$REPO_ROOT/build/$BOARD"

if [ -z "${CMAKE_GENERATOR:-}" ] && [ ! -f "$BUILD_DIR/CMakeCache.txt" ] &&
        command -v ninja >/dev/null 2>&1; then
    CMAKE_GENERATOR=Ninja
    export CMAKE_GENERATOR
fi

git -C "$REPO_ROOT" submodule update --init --recursive firmware/third_party

cmake -S "$REPO_ROOT/firmware" -B "$BUILD_DIR" \
    -DPICO_BOARD="$BOARD" \
    ${FIRMWARE_VERSION:+-DPICO_PWM_FIRMWARE_VERSION="$FIRMWARE_VERSION"}
cmake --build "$BUILD_DIR" --parallel
