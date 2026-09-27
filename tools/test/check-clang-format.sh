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
    diff_args="$BASE_SHA $HEAD_SHA"
else
    diff_args="${HEAD_SHA}^ $HEAD_SHA"
fi

tmp_ranges=$(mktemp)
trap 'rm -f "$tmp_ranges"' EXIT

git diff --unified=0 --diff-filter=ACMR $diff_args -- '*.c' '*.h' |
    awk '
        /^diff --git / {
            file = $4
            sub(/^b\//, "", file)
        }
        /^@@ / {
            range = $3
            sub(/^\+/, "", range)
            split(range, parts, ",")
            start = parts[1]
            count = parts[2]
            if (count == "") count = 1
            if (count > 0) print file, start, start + count - 1
        }
    ' >"$tmp_ranges"

if [ ! -s "$tmp_ranges" ]; then
    echo "clang-format: no changed C files"
    exit 0
fi

status=0
while read -r file start end; do
    if [ ! -f "$file" ]; then
        continue
    fi
    if ! clang-format --dry-run --Werror --style=file --lines "$start:$end" "$file"; then
        echo "clang-format: $file lines $start-$end are not formatted" >&2
        status=1
    fi
done <"$tmp_ranges"

exit "$status"
