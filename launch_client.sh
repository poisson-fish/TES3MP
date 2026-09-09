#!/usr/bin/env bash
# launch_client.sh
# Starts the OpenMW client with TES3MP multiplayer and Steam Morrowind assets on Linux and macOS.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$SCRIPT_DIR"

DATA_DIR="${MORROWIND_DATA_DIR:-}"
OPENMW_BIN="${OPENMW_BIN:-}"
RESOURCES_DIR="${OPENMW_RESOURCES:-}"
CONNECT_ADDR=""
PASSWORD=""
NO_GRAB=0
EXTRA_ARGS=()

# Parse arguments
while [[ $# -gt 0 ]]; do
    case "$1" in
        --data-dir=*)
            DATA_DIR="${1#*=}"
            shift
            ;;
        --data-dir)
            DATA_DIR="$2"
            shift 2
            ;;
        --bin=*)
            OPENMW_BIN="${1#*=}"
            shift
            ;;
        --bin)
            OPENMW_BIN="$2"
            shift 2
            ;;
        --connect=*)
            CONNECT_ADDR="${1#*=}"
            shift
            ;;
        --connect)
            CONNECT_ADDR="$2"
            shift 2
            ;;
        --password=*)
            PASSWORD="${1#*=}"
            shift
            ;;
        --password)
            PASSWORD="$2"
            shift 2
            ;;
        --no-grab)
            NO_GRAB=1
            shift
            ;;
        *)
            EXTRA_ARGS+=("$1")
            shift
            ;;
    esac
done

# 1. Locate OpenMW client binary
if [[ -z "$OPENMW_BIN" ]]; then
    BIN_CANDIDATES=(
        "$REPO_ROOT/build/vnext-baseline-install/bin/openmw"
        "$REPO_ROOT/build/vnext-baseline/openmw"
        "$REPO_ROOT/build/vnext-baseline/OpenMW.app/Contents/MacOS/openmw"
        "$REPO_ROOT/build/vnext-baseline-install/OpenMW.app/Contents/MacOS/openmw"
    )
    for cand in "${BIN_CANDIDATES[@]}"; do
        if [[ -x "$cand" ]]; then
            OPENMW_BIN="$cand"
            break
        fi
    done
fi

if [[ -z "$OPENMW_BIN" || ! -x "$OPENMW_BIN" ]]; then
    echo "Error: Could not find openmw executable." >&2
    echo "Please build OpenMW or set OPENMW_BIN." >&2
    exit 1
fi

# 2. Locate Morrowind Data Files
if [[ -z "$DATA_DIR" ]]; then
    STEAM_CANDIDATES=(
        "$HOME/.local/share/Steam/steamapps/common/Morrowind/Data Files"
        "$HOME/.steam/steam/steamapps/common/Morrowind/Data Files"
        "$HOME/.steam/root/steamapps/common/Morrowind/Data Files"
        "$HOME/.var/app/com.valvesoftware.Steam/.local/share/Steam/steamapps/common/Morrowind/Data Files"
        "$HOME/Library/Application Support/Steam/steamapps/common/Morrowind/Data Files"
    )
    for cand in "${STEAM_CANDIDATES[@]}"; do
        if [[ -f "$cand/Morrowind.esm" ]]; then
            DATA_DIR="$cand"
            break
        fi
    done
fi

if [[ -z "$DATA_DIR" || ! -f "$DATA_DIR/Morrowind.esm" ]]; then
    echo "Error: Could not find Morrowind Data Files containing Morrowind.esm." >&2
    echo "Please specify with --data-dir or set MORROWIND_DATA_DIR." >&2
    exit 1
fi

# 3. Locate Resources directory
if [[ -z "$RESOURCES_DIR" ]]; then
    BIN_DIR="$(dirname "$OPENMW_BIN")"
    RES_CANDIDATES=(
        "$BIN_DIR/resources"
        "$BIN_DIR/../Resources"
        "$REPO_ROOT/build/vnext-baseline-install/resources"
        "$REPO_ROOT/build/vnext-baseline/resources"
    )
    for cand in "${RES_CANDIDATES[@]}"; do
        if [[ -d "$cand" ]]; then
            RESOURCES_DIR="$cand"
            break
        fi
    done
fi

# 4. Build argument list
CLIENT_ARGS=(
    "--tes3mp-enable=1"
    "--data=$DATA_DIR"
)

if [[ -n "$RESOURCES_DIR" && -d "$RESOURCES_DIR" ]]; then
    CLIENT_ARGS+=("--resources=$RESOURCES_DIR")
fi

# BSAs (case-insensitive check if possible, standard names)
for bsa in "Morrowind.bsa" "Tribunal.bsa" "Bloodmoon.bsa"; do
    if [[ -f "$DATA_DIR/$bsa" ]]; then
        CLIENT_ARGS+=("--fallback-archive=$bsa")
    fi
done

# ESMs
for esm in "Morrowind.esm" "Tribunal.esm" "Bloodmoon.esm"; do
    if [[ -f "$DATA_DIR/$esm" ]]; then
        CLIENT_ARGS+=("--content=$esm")
    fi
done

if [[ "$NO_GRAB" -eq 1 ]]; then
    CLIENT_ARGS+=("--no-grab=1")
fi

TEMP_PASSWORD_FILE=""
cleanup() {
    if [[ -n "$TEMP_PASSWORD_FILE" && -f "$TEMP_PASSWORD_FILE" ]]; then
        rm -f "$TEMP_PASSWORD_FILE"
    fi
}
trap cleanup EXIT

if [[ -n "$CONNECT_ADDR" ]]; then
    HOST_PART="${CONNECT_ADDR%:*}"
    PORT_PART="25565"
    if [[ "$CONNECT_ADDR" == *:* ]]; then
        PORT_PART="${CONNECT_ADDR##*:}"
    fi

    CLIENT_ARGS+=(
        "--skip-menu=1"
        "--new-game=1"
        "--tes3mp-host=$HOST_PART"
        "--tes3mp-port=$PORT_PART"
    )

    if [[ -n "$PASSWORD" ]]; then
        TEMP_PASSWORD_FILE="$(mktemp)"
        printf "%s" "$PASSWORD" > "$TEMP_PASSWORD_FILE"
        CLIENT_ARGS+=("--tes3mp-password-file=$TEMP_PASSWORD_FILE")
    fi
fi

echo "=========================================="
echo "Starting TES3MP Client (OpenMW)"
echo "Binary    : $OPENMW_BIN"
echo "Data Files: $DATA_DIR"
if [[ -n "$RESOURCES_DIR" ]]; then echo "Resources : $RESOURCES_DIR"; fi
if [[ -n "$CONNECT_ADDR" ]]; then echo "Connect   : $CONNECT_ADDR"; fi
echo "=========================================="

exec "$OPENMW_BIN" "${CLIENT_ARGS[@]}" ${EXTRA_ARGS[@]+"${EXTRA_ARGS[@]}"}
