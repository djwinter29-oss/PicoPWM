#!/usr/bin/env sh
set -eu

BASE_SHA=""
HEAD_SHA="HEAD"

while [ "$#" -gt 0 ]; do
    case "$1" in
        --base)
            BASE_SHA="$2"
            shift 2
            ;;
        --head)
            HEAD_SHA="$2"
            shift 2
            ;;
        *)
            echo "Unknown argument: $1" >&2
            exit 1
            ;;
    esac
done

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname "$0")" && pwd)
REPO_ROOT=$(CDPATH= cd -- "$SCRIPT_DIR/../.." && pwd)
cd "$REPO_ROOT"

if ! command -v clang-format >/dev/null 2>&1; then
    echo "clang-format is required" >&2
    exit 1
fi

if [ -z "$BASE_SHA" ]; then
    BASE_SHA=$(git rev-parse "$HEAD_SHA^" 2>/dev/null || true)
fi

if [ -n "$BASE_SHA" ] && git cat-file -e "$BASE_SHA^{commit}" 2>/dev/null; then
    files=$(git diff --name-only --diff-filter=ACMR "$BASE_SHA" "$HEAD_SHA" -- \
        '*.c' '*.h')
else
    files=$(git ls-files -- '*.c' '*.h')
fi

if [ -z "$files" ]; then
    echo "clang-format: no changed C files"
    exit 0
fi

status=0
for file in $files; do
    if [ ! -f "$file" ]; then
        continue
    fi
    if ! clang-format --dry-run --Werror --style=file "$file"; then
        echo "clang-format: $file is not formatted" >&2
        status=1
    fi
done

exit "$status"
