#!/usr/bin/env sh
set -eu

BUILD_DIR="${BUILD_DIR:-}"
PROFILE="${PROFILE:-}"
BOARD="${PICO_BOARD:-}"
GENERATOR="${GENERATOR:-}"
PICO_SDK_PATH_VALUE="${PICO_SDK_PATH:-}"
FIRMWARE_VERSION="${PICO_PWM_FIRMWARE_VERSION:-}"

while [ "$#" -gt 0 ]; do
    case "$1" in
        --build-dir)
            BUILD_DIR="$2"
            shift 2
            ;;
        --profile)
            PROFILE="$2"
            shift 2
            ;;
        --board)
            BOARD="$2"
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

# Determine build directory
if [ -z "$BUILD_DIR" ]; then
    if [ -n "$PROFILE" ]; then
        # If profile is specified, use build/<profile>
        BUILD_DIR="build/$PROFILE"
    elif [ -n "$BOARD" ]; then
        # If board is specified, use build/<board>
        BUILD_DIR="build/$BOARD"
    else
        # Default: root-level build directory
        BUILD_DIR="build"
    fi
fi


if [ -n "$BOARD" ]; then
    case "$BOARD" in
        pico|pico2)
            ;;
        *)
            echo "Unsupported board '$BOARD'. Use 'pico' or 'pico2'." >&2
            exit 1
            ;;
    esac
fi

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname "$0")" && pwd)
REPO_ROOT=$(CDPATH= cd -- "$SCRIPT_DIR/../.." && pwd)
SOURCE_DIR="$REPO_ROOT/firmware"

if [ -z "$PICO_SDK_PATH_VALUE" ]; then
    PICO_SDK_PATH_VALUE="$SOURCE_DIR/third_party/pico-sdk"
fi
git -C "$REPO_ROOT" submodule update --init --recursive firmware/third_party
BUILD_DIR_PATH="$REPO_ROOT/$BUILD_DIR"

if [ -z "$GENERATOR" ]; then
    if command -v ninja >/dev/null 2>&1; then
        GENERATOR="Ninja"
    else
        GENERATOR="Unix Makefiles"
    fi
fi

set -- \
    -S "$SOURCE_DIR" \
    -B "$BUILD_DIR_PATH" \
    -G "$GENERATOR" \
    -DPICO_SDK_PATH="$PICO_SDK_PATH_VALUE"

if [ -n "$FIRMWARE_VERSION" ]; then
    set -- "$@" -DPICO_PWM_FIRMWARE_VERSION="$FIRMWARE_VERSION"
fi

if [ -n "$BOARD" ]; then
    set -- "$@" -DPICO_BOARD="$BOARD"
fi

cmake "$@"
cmake --build "$BUILD_DIR_PATH" --parallel