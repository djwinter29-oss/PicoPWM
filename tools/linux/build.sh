#!/usr/bin/env sh
set -eu

BUILD_DIR="${BUILD_DIR:-firmware/build}"
GENERATOR="${GENERATOR:-}"
PICO_SDK_PATH_VALUE="${PICO_SDK_PATH:-}"
FIRMWARE_VERSION="${PICO_PWM_FIRMWARE_VERSION:-}"

while [ "$#" -gt 0 ]; do
    case "$1" in
        --build-dir)
            BUILD_DIR="$2"
            shift 2
            ;;
        --generator)
            GENERATOR="$2"
            shift 2
            ;;
        --pico-sdk-path)
            PICO_SDK_PATH_VALUE="$2"
            shift 2
            ;;
        --firmware-version)
            FIRMWARE_VERSION="$2"
            shift 2
            ;;
        *)
            echo "Unknown argument: $1" >&2
            exit 1
            ;;
    esac
done

if [ -z "$PICO_SDK_PATH_VALUE" ]; then
    echo "PICO_SDK_PATH is not set." >&2
    exit 1
fi

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname "$0")" && pwd)
REPO_ROOT=$(CDPATH= cd -- "$SCRIPT_DIR/../.." && pwd)
SOURCE_DIR="$REPO_ROOT/firmware"
BUILD_DIR_PATH="$REPO_ROOT/$BUILD_DIR"

if [ -z "$GENERATOR" ]; then
    if command -v ninja >/dev/null 2>&1; then
        GENERATOR="Ninja"
    else
        GENERATOR="Unix Makefiles"
    fi
fi

if [ -n "$FIRMWARE_VERSION" ]; then
    cmake -S "$SOURCE_DIR" -B "$BUILD_DIR_PATH" -G "$GENERATOR" -DPICO_SDK_PATH="$PICO_SDK_PATH_VALUE" -DPICO_PWM_FIRMWARE_VERSION="$FIRMWARE_VERSION"
else
    cmake -S "$SOURCE_DIR" -B "$BUILD_DIR_PATH" -G "$GENERATOR" -DPICO_SDK_PATH="$PICO_SDK_PATH_VALUE"
fi
cmake --build "$BUILD_DIR_PATH" --parallel