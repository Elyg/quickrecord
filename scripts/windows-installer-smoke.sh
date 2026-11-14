#!/usr/bin/env bash
# Installs the release installer silently into a temporary folder, checks what it put there,
# then uninstalls and checks it's all gone (settings and recordings aren't touched). CI only.
set -euo pipefail
# Git Bash would turn /S and /D=… into paths.
export MSYS2_ARG_CONV_EXCL='*'

setup=$(ls QuickRecord-*-windows-x64-setup.exe)
dir="$(cygpath -w "$(mktemp -d)")\\QuickRecord"
key='HKCU\Software\Microsoft\Windows\CurrentVersion\Uninstall\QuickRecord'
unix=$(cygpath -u "$dir")

# /D= must come last and unquoted.
"./$setup" /S /D=$dir
[ -f "$unix/QuickRecord.exe" ] || { echo "Not installed into $dir"; exit 1; }
[ -f "$unix/ffmpeg.exe" ] || { echo "ffmpeg.exe missing from $dir"; exit 1; }
reg query "$key" /v InstallLocation | tee /dev/stderr | grep -qF "$dir"
echo "Installed into $dir"

# The uninstaller runs a copy of itself from %TEMP% and returns at once: wait for the folder.
"$unix/Uninstall.exe" /S
for _ in $(seq 60); do
    [ -d "$unix" ] || break
    sleep 0.5
done
[ ! -d "$unix" ] || { echo "Uninstall left $dir behind"; ls -la "$unix"; exit 1; }
if reg query "$key" >/dev/null 2>&1; then
    echo "Uninstall left $key behind"
    exit 1
fi
echo "Uninstalled"
