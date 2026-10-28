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
Restores NuGet dependencies and builds the host before starting.
Requires the .NET 10 ASP.NET Core runtime, or an explicit DOTNET_ROLL_FORWARD policy.
The host talks to the board over USB HID; optionally set PICOPWM_SERIAL or
PICOPWM_DEVICE_PATH to preselect a board.
Set PICOPWM_HOST / PICOPWM_PORT (defaults 127.0.0.1:5000), ASPNETCORE_URLS,
or pass --urls http://127.0.0.1:5000 to select the address.
Set PICOPWM_TOKEN before listening beyond loopback. The host refuses a remote URL without it.
EOF
        exit 0
        ;;
esac

    "$SCRIPT_DIR/hid-permissions-check.sh"

if ! command -v "$DOTNET_EXE" >/dev/null 2>&1; then
    echo ".NET SDK executable not found: $DOTNET_EXE; install the .NET SDK or set DOTNET." >&2
    exit 1
fi
if ! "$DOTNET_EXE" --list-sdks | awk -F. '$1 >= 10 { found = 1 } END { exit !found }'; then
    echo ".NET 10 SDK or newer not found; install it or set DOTNET to its executable." >&2
    exit 1
fi
if [ -z "${DOTNET_ROLL_FORWARD:-}" ] &&
    ! "$DOTNET_EXE" --list-runtimes | grep -q '^Microsoft.AspNetCore.App 10\.'; then
    echo "The .NET 10 ASP.NET Core runtime is missing. Install it, or explicitly set DOTNET_ROLL_FORWARD=Major to use a newer installed runtime." >&2
    exit 1
fi
ASPNETCORE_URLS="${ASPNETCORE_URLS:-http://${PICOPWM_HOST:-127.0.0.1}:${PICOPWM_PORT:-5000}}"
export ASPNETCORE_URLS
echo "Restoring and building PicoPWM .NET dashboard (default address: $ASPNETCORE_URLS; --urls overrides it)."
exec "$DOTNET_EXE" run --project "$REPO_ROOT/host/dotnet/PicoPwm" --no-launch-profile -- "$@"
