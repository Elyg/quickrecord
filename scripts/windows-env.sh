# Source me. The Windows counterpart of the flake's shellHook, since nix doesn't run there:
# fills .sdk/ with what a Windows build needs (Qt, llvm-mingw, and the ffmpeg + gifski that
# go into the zip) and exports the same variables. `just` sources it by itself on Windows.
#
# Works from macOS and Linux too, inside the devShell: see `just windows-check`.

# Versions live in the flake; only what has no Linux/macOS use is pinned here.
QT_VER=$(sed -n 's/.*qtVer = "\(.*\)";.*/\1/p' flake.nix)
GIFSKI_VER=$(sed -n 's/.*gifskiVer = "\(.*\)";.*/\1/p' flake.nix)
# The llvm-mingw of Qt's win64_llvm_mingw (Clang 17): libc++.dll comes from Qt's bin/.
LLVM_MINGW_VER=20231128
# Tag of a release branch: rolling, but only with that branch's fixes.
FFMPEG_VER=8.1

# C:/… on Windows: cmake and conan are native programs, and $PWD's /c/… means nothing to them.
export SDK_ROOT="$(pwd -W 2>/dev/null || pwd)/.sdk"
export QT_VER
export QT_INSTALL_ROOT="$SDK_ROOT/Qt/$QT_VER"
export BUNDLE_TOOLS_DIR="$SDK_ROOT/tools/windows-bundle"
export SHELL_TYPE="host"

_sdk_fetch() { # url, file
    curl -fsSL --retry 3 "$1" -o "$2"
}

_sdk_unzip() { # zip, directory
    "$(command -v python3 || command -v python)" -m zipfile -e "$1" "$2"
}

_sdk_setup() {
    local qt="$QT_INSTALL_ROOT/llvm-mingw_64" toolchain="$SDK_ROOT/llvm-mingw" asset dir tmp

    if [ ! -d "$qt/lib/cmake/Qt6Multimedia" ]; then
        mkdir -p "$SDK_ROOT/Qt"
        aqt install-qt windows desktop "$QT_VER" win64_llvm_mingw -m qtmultimedia -O "$SDK_ROOT/Qt" || return 1
    fi

    if [ ! -d "$toolchain" ]; then
        case "$(uname -s)" in
            Darwin) asset="llvm-mingw-$LLVM_MINGW_VER-ucrt-macos-universal.tar.xz" ;;
            Linux) asset="llvm-mingw-$LLVM_MINGW_VER-ucrt-ubuntu-20.04-x86_64.tar.xz" ;;
            *) asset="llvm-mingw-$LLVM_MINGW_VER-ucrt-x86_64.zip" ;;
        esac
        tmp=$(mktemp -d)
        _sdk_fetch "https://github.com/mstorsjo/llvm-mingw/releases/download/$LLVM_MINGW_VER/$asset" "$tmp/$asset" || return 1
        case "$asset" in
            *.zip) _sdk_unzip "$tmp/$asset" "$tmp" || return 1; dir=${asset%.zip} ;;
            *) tar -xJf "$tmp/$asset" -C "$tmp" || return 1; dir=${asset%.tar.xz} ;;
        esac
        mv "$tmp/$dir" "$toolchain" || return 1
        rm -rf "$tmp"
    fi

    if [ ! -f "$BUNDLE_TOOLS_DIR/ffmpeg.exe" ]; then
        mkdir -p "$BUNDLE_TOOLS_DIR"
        tmp=$(mktemp -d)
        _sdk_fetch "https://github.com/BtbN/FFmpeg-Builds/releases/download/latest/ffmpeg-n$FFMPEG_VER-latest-win64-gpl-$FFMPEG_VER.zip" "$tmp/ffmpeg.zip" || return 1
        _sdk_unzip "$tmp/ffmpeg.zip" "$tmp" || return 1
        cp "$tmp"/ffmpeg-*/bin/ffmpeg.exe "$BUNDLE_TOOLS_DIR/" || return 1
        cp "$tmp"/ffmpeg-*/LICENSE.txt "$BUNDLE_TOOLS_DIR/LICENSE.ffmpeg.txt" || return 1
        rm -rf "$tmp"
    fi

    if [ ! -f "$BUNDLE_TOOLS_DIR/gifski.exe" ]; then
        mkdir -p "$BUNDLE_TOOLS_DIR"
        tmp=$(mktemp -d)
        _sdk_fetch "https://github.com/ImageOptim/gifski/releases/download/$GIFSKI_VER/gifski-$GIFSKI_VER.tar.xz" "$tmp/gifski.tar.xz" || return 1
        tar -xJf "$tmp/gifski.tar.xz" -C "$tmp" || return 1
        cp "$tmp/win/gifski.exe" "$BUNDLE_TOOLS_DIR/" || return 1
        cp "$tmp/LICENSE" "$BUNDLE_TOOLS_DIR/LICENSE.gifski.txt" || return 1
        rm -rf "$tmp"
    fi

    # A POSIX PATH: its separator is ":" here, so no C:/… in it. Not $PWD as it stands: a bash
    # started through a native program, like `just`, inherits it as D:/a/….
    local root
    root=$(cygpath -u "$PWD" 2>/dev/null || pwd)
    export PATH="$root/.sdk/llvm-mingw/bin:$root/.sdk/Qt/$QT_VER/llvm-mingw_64/bin:$PATH"
}

_sdk_setup
