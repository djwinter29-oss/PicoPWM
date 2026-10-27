#!/usr/bin/env sh
set -eu

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
REPO_ROOT=$(CDPATH= cd -- "$SCRIPT_DIR/../.." && pwd)
DOTNET_EXE="${DOTNET:-dotnet}"

case "${1:-}" in
    -h|--help)
        cat <<'EOF'
Usage: tools/host/run-dotnet.sh [dotnet application arguments...]

Run the .NET web host. Requires the .NET SDK; set DOTNET to select its executable.
The host talks to the board over USB HID; optionally set PICOPWM_SERIAL or
PICOPWM_DEVICE_PATH to pick a board. Pass --urls http://127.0.0.1:5000 to pick the address.
EOF
        exit 0
        ;;
esac

if ! command -v "$DOTNET_EXE" >/dev/null 2>&1; then
    echo ".NET SDK executable not found: $DOTNET_EXE; install the .NET SDK or set DOTNET." >&2
    exit 1
fi
exec "$DOTNET_EXE" run --project "$REPO_ROOT/host/dotnet/PicoPwm" --no-launch-profile -- "$@"
