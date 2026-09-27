#!/usr/bin/env sh
set -eu

BUILD_DIR="${BUILD_DIR:-firmware/build/tests}"
GENERATOR="${GENERATOR:-}"
SKIP_BUILD=0

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
        --skip-build)
            SKIP_BUILD=1
            shift
            ;;
        *)
            echo "Unknown argument: $1" >&2
            exit 1
            ;;
    esac
done

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname "$0")" && pwd)
REPO_ROOT=$(CDPATH= cd -- "$SCRIPT_DIR/../.." && pwd)
BUILD_DIR_PATH="$REPO_ROOT/$BUILD_DIR"

if [ "$SKIP_BUILD" -eq 0 ]; then
    if [ -n "$GENERATOR" ]; then
        cmake -S "$REPO_ROOT/firmware/tests" -B "$BUILD_DIR_PATH" -G "$GENERATOR"
    else
        cmake -S "$REPO_ROOT/firmware/tests" -B "$BUILD_DIR_PATH"
    fi
    cmake --build "$BUILD_DIR_PATH" --parallel
fi

ctest --test-dir "$BUILD_DIR_PATH" --output-on-failure
