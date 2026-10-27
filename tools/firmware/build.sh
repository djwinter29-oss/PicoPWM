#!/usr/bin/env sh
# Usage: tools/firmware/build.sh [--board pico|pico2] [--firmware-version VERSION]
# Builds into build/<board>. The Pico SDK is the firmware/third_party/pico-sdk submodule; CMAKE_GENERATOR picks a generator.
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

git -C "$REPO_ROOT" submodule update --init --recursive firmware/third_party

cmake -S "$REPO_ROOT/firmware" -B "$BUILD_DIR" \
    -DPICO_BOARD="$BOARD" \
    ${FIRMWARE_VERSION:+-DPICO_PWM_FIRMWARE_VERSION="$FIRMWARE_VERSION"}
cmake --build "$BUILD_DIR" --parallel
