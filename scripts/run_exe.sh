#!/bin/bash
# Runs the built quickrecord binary: scripts/run_exe.sh [ARCH] [CONFIG] [APP_ARGS...]
set -euo pipefail

ARCH="${1:-linux-x64}"
CONFIG="${2:-Debug}"
shift 2 2>/dev/null || true

EXE_PATH_FILE="./build/${ARCH}-${CONFIG}/exe_path.txt"
if [ ! -f "$EXE_PATH_FILE" ]; then
    echo "Error: $EXE_PATH_FILE not found. Run 'just build $ARCH $CONFIG' first." >&2
    exit 1
fi
exePath=$(tr -d '\n\r' < "$EXE_PATH_FILE")

echo "Running: $exePath $*"
echo "----------------------------------------"
if command -v nixGLIntel >/dev/null; then
    nixGLIntel "$exePath" "$@"
else
    "$exePath" "$@"
fi
