#!/usr/bin/env bash
# Runs the release zip as a user would, with nothing of the build around it: unpack, start,
# record a few seconds through the scripted commands, and check what came out. CI only.
set -euo pipefail

dir=$(mktemp -d)
python -m zipfile -e QuickRecord-*-windows-x64.zip "$dir"
app="$dir/QuickRecord/QuickRecord.exe"
ffmpeg="$dir/QuickRecord/ffmpeg.exe"

# Only Windows' own directories: a DLL missing from the zip must fail here, not on a user's PC.
PATH="$(dirname "$(command -v sleep)"):/c/Windows/System32:/c/Windows"

"$app" &
daemon=$!
trap 'kill "$daemon" 2>/dev/null || true' EXIT

started=
for _ in $(seq 30); do
    if answer=$("$app" --record-rect 0,0,640,480 2>&1); then
        started=1
        break
    fi
    kill -0 "$daemon" || { echo "It died at startup: $answer"; exit 1; }
    sleep 1
done
[ -n "$started" ] || { echo "No running instance: $answer"; exit 1; }
echo "$answer"

sleep 3
answer=$("$app" --stop | tr -d '\r')
echo "$answer"
master=$(echo "$answer" | cut -f2)
"$app" --quit

info=$("$ffmpeg" -hide_banner -i "$master" 2>&1 || true)
echo "$info"
echo "$info" | grep -q 'Video: h264'
echo "$info" | grep -q '640x480'
