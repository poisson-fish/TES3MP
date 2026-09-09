#!/usr/bin/env bash
# launch_server.sh
# Starts the TES3MP dedicated server on Linux and macOS.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$SCRIPT_DIR"

CONFIG_PATH="${1:-${TES3MP_SERVER_CONFIG:-}}"
SERVER_BIN="${TES3MP_SERVER_BIN:-}"

# 1. Locate server binary
if [[ -z "$SERVER_BIN" ]]; then
    CANDIDATES=(
        "$REPO_ROOT/build/vnext-baseline-install/bin/tes3mp_server"
        "$REPO_ROOT/build/vnext-baseline/tes3mp-server/tes3mp_server"
        "$REPO_ROOT/build/vnext-baseline/tes3mp_server"
        "$REPO_ROOT/build/tes3mp-server/tes3mp_server"
        "$REPO_ROOT/build/vnext-baseline/OpenMW.app/Contents/MacOS/tes3mp_server"
    )
    for cand in "${CANDIDATES[@]}"; do
        if [[ -x "$cand" ]]; then
            SERVER_BIN="$cand"
            break
        fi
    done
fi

if [[ -z "$SERVER_BIN" || ! -x "$SERVER_BIN" ]]; then
    echo "Error: Could not find tes3mp_server executable." >&2
    echo "Please build the server or set TES3MP_SERVER_BIN." >&2
    exit 1
fi

# 2. Locate server configuration file
if [[ -z "$CONFIG_PATH" ]]; then
    CONFIG_CANDIDATES=(
        "$REPO_ROOT/build/vnext-baseline-install/resources/vfs/tes3mp/server.cfg"
        "$REPO_ROOT/build/vnext-baseline/resources/vfs/tes3mp/server.cfg"
        "$REPO_ROOT/files/data/tes3mp/server.cfg"
    )
    for cand in "${CONFIG_CANDIDATES[@]}"; do
        if [[ -f "$cand" ]]; then
            CONFIG_PATH="$cand"
            break
        fi
    done
fi

if [[ -z "$CONFIG_PATH" || ! -f "$CONFIG_PATH" ]]; then
    echo "Error: Could not find server.cfg." >&2
    echo "Please specify the configuration path as an argument or set TES3MP_SERVER_CONFIG." >&2
    exit 1
fi

echo "=========================================="
echo "Starting TES3MP Dedicated Server"
echo "Binary: $SERVER_BIN"
echo "Config: $CONFIG_PATH"
echo "=========================================="

exec "$SERVER_BIN" "$CONFIG_PATH"
