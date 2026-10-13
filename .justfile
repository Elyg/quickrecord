set shell := ["bash", "-c"]

PROJECT_NAME := "quickrecord"
default_arch := "linux-x64"
default_config := "Debug"

# List recipes
default:
    just --list --unsorted

# --- INITIALIZATION ---

# Fresh checkout → deps → configure → build → run
init: (conan-install "conan-linux-x64-clang" "Debug") build run

# --- SHELL DISPATCH ---

# Re-enter the nix devShell if we are not already inside it
_dispatch recipe_cmd:
    #!/usr/bin/env bash
    if [[ "$SHELL_TYPE" == "host" ]]; then
        just {{ recipe_cmd }}
    else
        nix develop -c just {{ recipe_cmd }}
    fi

# --- CONAN ---

conan-install profile="conan-linux-x64-clang" config=default_config:
    just _dispatch "_conan-install {{ profile }} {{ config }}"

_conan-install profile config:
    conan install . \
        -of="conan-build/{{ profile }}/{{ config }}" \
        --build=missing \
        -s build_type={{ config }} \
        -pr:h="./conan-profiles/{{ profile }}" \
        -pr:b="./conan-profiles/conan-linux-x64-clang"

# --- CMAKE WORKFLOW ---

setup arch=default_arch config=default_config:
    just _dispatch "_setup {{ arch }} {{ config }}"

build arch=default_arch config=default_config target='all': (setup arch config)
    just _dispatch "_build {{ arch }} {{ config }} {{ target }}"

install arch=default_arch config=default_config: (build arch config)
    just _dispatch "_install {{ arch }} {{ config }}"

# Run the tray app (pass e.g. `--toggle` after the config)
run arch=default_arch config=default_config *args="": (build arch config)
    just _dispatch "_run {{ arch }} {{ config }} {{ args }}"

tests arch=default_arch config=default_config: (build arch config)
    just _dispatch "_tests {{ arch }} {{ config }}"

# Build the AppImage (bundles static ffmpeg + gifski)
package arch=default_arch config="Release": (install arch config)
    just _dispatch "_package {{ arch }} {{ config }}"

_setup arch config:
    #!/usr/bin/env bash
    set -e
    if [ ! -f "conan-build/conan-{{ arch }}-clang/{{ config }}/build/{{ config }}/generators/conan_toolchain.cmake" ]; then
        just _conan-install conan-{{ arch }}-clang {{ config }}
    fi
    cmake --preset {{ arch }}-{{ config }}

_build arch config target:
    cmake --build build/{{ arch }}-{{ config }} --target {{ target }}

_install arch config:
    cmake --install build/{{ arch }}-{{ config }} --prefix install/{{ arch }}-{{ config }}

_run arch config *args:
    ./scripts/run_exe.sh {{ arch }} {{ config }} {{ args }}

_tests arch config:
    ./build/{{ arch }}-{{ config }}/bin/QuickRecordTests

_package arch config:
    #!/usr/bin/env bash
    set -euo pipefail
    export QMAKE="$QT_INSTALL_ROOT/gcc_64/bin/qmake"
    export PATH="$QT_INSTALL_ROOT/gcc_64/bin:$PATH"
    export LD_LIBRARY_PATH="$QT_INSTALL_ROOT/gcc_64/lib:$NIX_LIBS_PATH:$LD_LIBRARY_PATH"
    # Make sure the xcb platform plugin and the multimedia (FFmpeg) plugins get deployed.
    export EXTRA_PLATFORM_PLUGINS="libqxcb.so"
    export EXTRA_QT_MODULES="multimedia"

    VERSION=$(git describe --tags --abbrev=0 2>/dev/null | sed 's/^v//' || true)
    export LINUXDEPLOY_OUTPUT_VERSION="${VERSION:-dev}"
    VERSION="$LINUXDEPLOY_OUTPUT_VERSION"

    rm -rf ./AppDir
    mkdir -p AppDir/usr/lib AppDir/usr/bin AppDir/usr/share/licenses/quickrecord

    # The whole glibc set (libmvec, libresolv, nss, ...), not just libc: a host's older
    # copies of the rest are picked up from /usr/lib64 and fail on GLIBC_2.xx versions.
    cp $GLIBC_LIB_PATH/*.so.[0-9]* AppDir/usr/lib/
    # The C++ runtime is too new for most hosts' (e.g. Rocky 9 ships GCC 11), so bundle it.
    cp $GCC_LIB_PATH/libstdc++.so.6 $GCC_LIB_PATH/libgcc_s.so.1 AppDir/usr/lib/
    # Keep these read-only (as copied from the nix store): linuxdeploy would otherwise
    # patchelf an $ORIGIN rpath into ld-linux/libc, which makes the loader crash.

    # Static helper binaries (separate programs; licenses shipped alongside)
    cp "$BUNDLE_TOOLS_DIR/ffmpeg" "$BUNDLE_TOOLS_DIR/gifski" AppDir/usr/bin/
    # Read-only for the same reason as glibc: linuxdeploy's rpath patching corrupts
    # static(-pie) executables (gifski segfaults).
    chmod a-w AppDir/usr/bin/ffmpeg AppDir/usr/bin/gifski
    cp "$BUNDLE_TOOLS_DIR"/LICENSE.*.txt AppDir/usr/share/licenses/quickrecord/

    rsvg-convert -w 256 -h 256 icons/quickrecord.svg -o build/quickrecord.png

    linuxdeploy \
        --executable install/{{ arch }}-{{ config }}/bin/quickrecord \
        --appdir AppDir \
        --custom-apprun ./scripts/AppRun.template \
        --desktop-file ./scripts/quickrecord.desktop \
        --icon-file build/quickrecord.png \
        --icon-filename quickrecord \
        $APPIMAGE_LIBS_ARGS \
        --plugin qt \
        --output appimage

    # linuxdeploy names it <desktop Name>-<VERSION>-<arch>.AppImage
    CPU_ARCH=$(echo "{{ arch }}" | sed 's/linux-x64/x86_64/')
    OUT="QuickRecord-$VERSION-$CPU_ARCH.AppImage"
    test -f "$OUT"
    echo "Built $OUT"

# --- SECRETS (none needed yet; kept for the shared convention) ---

secrets-set:
    bash scripts/secrets-set.sh

secrets-list:
    #!/usr/bin/env bash
    echo "quickrecord keychain entries: (none defined)"

export-env:
    bash scripts/export-env.sh > .env

# --- CLEANING ---

clean_build:
    rm -rf ./build/* ./install ./AppDir

clean_conan:
    rm -rf ./conan-build/*

clean_all: clean_build clean_conan
    rm -rf ./.venv

# Tag a release and push it (triggers the CI build on the v* tag).
# With no argument, auto-bumps the patch of the latest v* tag (v0.0.7 → v0.0.8).
#
# This recipe is IDENTICAL across every one of the user's projects — copy it verbatim
# into any new project's .justfile. Pass an explicit tag for minor/major bumps:
#   just release           # auto patch bump
#   just release v1.0.0    # explicit
release tag="":
    #!/usr/bin/env bash
    set -euo pipefail
    tag="{{tag}}"
    if [ -z "$tag" ]; then
        last=$(git tag --list 'v*' --sort=-v:refname | head -1)
        last="${last:-v0.0.0}"
        IFS=. read -r major minor patch <<< "${last#v}"
        tag="v${major}.${minor}.$((patch + 1))"
        echo "Auto-bumping: ${last} → ${tag}"
    fi
    git tag "$tag"
    git push origin "$tag"
