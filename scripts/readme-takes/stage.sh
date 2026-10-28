#!/usr/bin/env bash
export XDG_CACHE_HOME=/Users/Shared/rega-guide/cache XDG_STATE_HOME=/Users/Shared/qr-demo/state XDG_CONFIG_HOME=/Users/Shared/qr-demo/config
export PATH=/Users/etitas/gits/rega/build/macos-arm64-Release:$PATH
cd /Users/Shared/rega-guide/cache/rega/tutorial
rega 'shot.####.exr' chart.exr hdr-sky.exr beachball.exr nan-inf.exr luma-ramp.exr
exec sleep 100000
