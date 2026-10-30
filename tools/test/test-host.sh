#!/usr/bin/env sh
# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026 Ji Dong

# Run every host-side test: Python, web assets, WebHID and .NET.
# Tools come from PYTHON, NODE and DOTNET (defaults python3, node, dotnet).
set -eu

REPO_ROOT=$(CDPATH= cd -- "$(dirname "$0")/../.." && pwd)
PYTHON=${PYTHON:-python3}
NODE=${NODE:-node}
DOTNET=${DOTNET:-dotnet}

for tool in "$PYTHON" "$NODE" "$DOTNET"; do
    if ! command -v "$tool" >/dev/null 2>&1; then
        printf 'Required host test tool not found: %s\n' "$tool" >&2
        exit 1
    fi
done

printf '%s\n' '=== Python host tests ==='
"$PYTHON" -m pytest -q "$REPO_ROOT/host/python/tests"

printf '%s\n' '=== Host launcher tests ==='
"$PYTHON" "$REPO_ROOT/tools/test/test_run_python.py"
"$PYTHON" "$REPO_ROOT/tools/test/test_run_dotnet.py"

printf '%s\n' '=== HIL runner unit tests (no hardware) ==='
"$PYTHON" -m unittest discover -s "$REPO_ROOT/tools/hil/tests" -p 'test_*.py'

printf '%s\n' '=== Web and WebHID tests ==='
"$NODE" --test "$REPO_ROOT"/host/web/tests/*.test.mjs "$REPO_ROOT"/host/webhid/tests/*.test.mjs

printf '%s\n' '=== .NET host tests ==='
"$DOTNET" test "$REPO_ROOT/host/dotnet/PicoPwm.sln" --configuration Release --nologo
