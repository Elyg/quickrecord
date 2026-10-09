{
  description = "quickrecord — Qt 6.10 + Clang 20 dev environment";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
    flake-utils.url = "github:numtide/flake-utils";
    nixgl.url = "github:nix-community/nixGL";
  };

  outputs =
    {
      self,
      nixpkgs,
      flake-utils,
      nixgl,
    }:
    flake-utils.lib.eachDefaultSystem (
      system:
      let
        pkgs = import nixpkgs {
          inherit system;
          config.allowUnfree = true;
        };

        # --- CONFIGURATION ---
        qtVer = "6.10.1";
        # Tools bundled into the AppImage (static binaries, see `just package`)
        gifskiVer = "1.34.0";

        hostLlvm = pkgs.llvmPackages_20;
        nixgl_pkg = if pkgs.stdenv.isLinux then nixgl.packages.${system}.nixGLIntel else pkgs.coreutils;

        # --- LIBRARIES ---
        libList =
          p: with p; [
            libGL
            libglvnd
            xorg.libX11
            xorg.libxcb
            xorg.libXext
            xorg.libXfixes
            xorg.xcbutil
            xorg.xcbutilimage
            xorg.xcbutilkeysyms
            xorg.xcbutilrenderutil
            xorg.xcbutilwm
            xorg.libxkbfile
            xorg.xcbutilcursor
            libxkbcommon
            libpulseaudio
            libkrb5
            brotli
            bzip2
            libdrm
            xorg.libXrandr
            fontconfig
            dbus
            glib
            zlib
            zstd
            freetype
            harfbuzz
            icu
          ];

        # --- APPIMAGE LIBS TO PACKAGE ---
        appImageLibs = [
          "${pkgs.fontconfig.lib}/lib/libfontconfig.so.1"
          "${pkgs.freetype}/lib/libfreetype.so.6"
          "${pkgs.glib.out}/lib/libglib-2.0.so.0"
          "${pkgs.libglvnd}/lib/libGL.so.1"
          "${pkgs.libglvnd}/lib/libEGL.so.1"
          "${pkgs.libglvnd}/lib/libGLX.so.0"
          "${pkgs.libglvnd}/lib/libOpenGL.so.0"
          "${pkgs.libglvnd}/lib/libGLdispatch.so.0"
          "${pkgs.expat}/lib/libexpat.so.1"
          "${pkgs.xorg.libX11}/lib/libX11.so.6"
          "${pkgs.zlib}/lib/libz.so.1"
          "${pkgs.xorg.libxcb}/lib/libxcb.so.1"
          "${pkgs.xorg.xcbutilkeysyms}/lib/libxcb-keysyms.so.1"
          "${pkgs.libxkbcommon}/lib/libxkbcommon.so.0"
          "${pkgs.dbus.lib}/lib/libdbus-1.so.3"
          "${pkgs.xorg.libXext}/lib/libXext.so.6"
          "${pkgs.libpng}/lib/libpng16.so.16"
          "${pkgs.pcre2.out}/lib/libpcre2-8.so.0"
          "${pkgs.brotli.lib}/lib/libbrotlidec.so.1"
          "${pkgs.xcb-util-cursor}/lib/libxcb-cursor.so.0"
          "${pkgs.xorg.libX11}/lib/libX11-xcb.so.1"
          "${pkgs.xorg.libXau}/lib/libXau.so.6"
          "${pkgs.xorg.libXdmcp}/lib/libXdmcp.so.6"
        ];

        appImageLibsArgs = builtins.concatStringsSep " " (map (l: "--library \"${l}\"") appImageLibs);

        # --- ENV VARIABLES ---
        libraryEnv = {
          GLIBC_LIB_PATH = "${pkgs.glibc.out}/lib";
          NIX_LIBS_PATH = "${pkgs.lib.makeLibraryPath (libList pkgs)}:${pkgs.stdenv.cc.cc.lib}/lib";
          APPIMAGE_LIBS_ARGS = appImageLibsArgs;
        };

        shellSetup = ''
          # Avoid non nix python modules from host
          unset PYTHONPATH

          # Python venv (only used for aqtinstall)
          if [ -d .venv ] && ! ./.venv/bin/python --version >/dev/null 2>&1; then
              echo "Detecting broken venv, recreating..."
              rm -rf .venv
          fi
          if [ ! -d .venv ]; then
              echo "Creating missing venv..." >&2
              uv venv
          fi

          export SDK_ROOT="$PWD/.sdk"
          export QT_VER="${qtVer}"
          export QT_INSTALL_ROOT="$SDK_ROOT/Qt/$QT_VER"
          export QT_HOST_PATH="$QT_INSTALL_ROOT/gcc_64"
          export BUNDLE_TOOLS_DIR="$SDK_ROOT/tools/bundle"

          export VIRTUAL_ENV="$PWD/.venv"
          export PATH="$VIRTUAL_ENV/bin:$PATH"
          export PATH="$SDK_ROOT/tools:$PATH"

          ${builtins.concatStringsSep "\n" (
            pkgs.lib.mapAttrsToList (k: v: "export ${k}=\"${v}\"") libraryEnv
          )}

          # Qt (+ Multimedia for the editor's video preview) via aqt into .sdk/
          if [ ! -f .venv/bin/aqt ]; then uv pip install -q aqtinstall; fi
          if [ ! -d "$QT_INSTALL_ROOT/gcc_64/lib/cmake/Qt6Multimedia" ]; then
              mkdir -p "$SDK_ROOT/Qt"
              ./.venv/bin/aqt install-qt linux desktop "$QT_VER" linux_gcc_64 -m qtmultimedia -O "$SDK_ROOT/Qt"
          fi

          # linuxdeploy (+ qt plugin), extracted so they run without FUSE
          install_appimage() {
              local url=$1; local name=$2; local dir_name="$name-root"; local final_link="$SDK_ROOT/tools/$name"
              if [ ! -e "$final_link" ]; then
                  mkdir -p "$SDK_ROOT/tools"
                  local tmp_dir=$(mktemp -d)
                  wget -q "$url" -O "$tmp_dir/$name.AppImage"
                  chmod +x "$tmp_dir/$name.AppImage"
                  (cd "$tmp_dir" && ./$name.AppImage --appimage-extract >/dev/null)
                  rm -rf "$SDK_ROOT/tools/$dir_name"
                  mv "$tmp_dir/squashfs-root" "$SDK_ROOT/tools/$dir_name"
                  ln -sf "$SDK_ROOT/tools/$dir_name/AppRun" "$final_link"
                  rm -rf "$tmp_dir"
              fi
          }
          install_appimage "https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-x86_64.AppImage" "linuxdeploy"
          install_appimage "https://github.com/linuxdeploy/linuxdeploy-plugin-qt/releases/download/continuous/linuxdeploy-plugin-qt-x86_64.AppImage" "linuxdeploy-plugin-qt"

          # Static ffmpeg + gifski shipped inside the AppImage (NOT put on PATH;
          # the dev shell uses the nix ffmpeg/gifski below).
          if [ ! -x "$BUNDLE_TOOLS_DIR/ffmpeg" ]; then
              mkdir -p "$BUNDLE_TOOLS_DIR"
              tmp_dir=$(mktemp -d)
              wget -q "https://johnvansickle.com/ffmpeg/releases/ffmpeg-release-amd64-static.tar.xz" -O "$tmp_dir/ffmpeg.tar.xz"
              tar -xJf "$tmp_dir/ffmpeg.tar.xz" -C "$tmp_dir"
              cp "$tmp_dir"/ffmpeg-*-static/ffmpeg "$BUNDLE_TOOLS_DIR/"
              cp "$tmp_dir"/ffmpeg-*-static/GPLv3.txt "$BUNDLE_TOOLS_DIR/LICENSE.ffmpeg.txt"
              rm -rf "$tmp_dir"
          fi
          if [ ! -x "$BUNDLE_TOOLS_DIR/gifski" ]; then
              mkdir -p "$BUNDLE_TOOLS_DIR"
              tmp_dir=$(mktemp -d)
              wget -q "https://github.com/ImageOptim/gifski/releases/download/${gifskiVer}/gifski-${gifskiVer}.tar.xz" -O "$tmp_dir/gifski.tar.xz"
              tar -xJf "$tmp_dir/gifski.tar.xz" -C "$tmp_dir"
              cp "$tmp_dir"/linux/gifski "$BUNDLE_TOOLS_DIR/"
              chmod +x "$BUNDLE_TOOLS_DIR/gifski"
              cp "$tmp_dir"/LICENSE "$BUNDLE_TOOLS_DIR/LICENSE.gifski.txt" 2>/dev/null || true
              rm -rf "$tmp_dir"
          fi

          # Force Nix Build Tools
          export CMAKE_COMMAND="${pkgs.cmake}/bin/cmake"
          export CMAKE_MAKE_PROGRAM="${pkgs.ninja}/bin/ninja"
          export PATH="${pkgs.cmake}/bin:${pkgs.ninja}/bin:$PATH"
        '';
      in
      {
        devShells.default = pkgs.mkShell {
          nativeBuildInputs =
            (with pkgs; [
              cmake
              ninja
              just
              gum
              python3
              uv
              conan
              wget
              curl
              git
              file
              patchelf
              desktop-file-utils
              librsvg
              pkg-config
              ffmpeg
              gifski
              hostLlvm.clang
              hostLlvm.bintools
              hostLlvm.clang-tools
              gdb
            ])
            ++ (pkgs.lib.optional pkgs.stdenv.isLinux nixgl_pkg);

          buildInputs = libList pkgs;

          shellHook = shellSetup + ''
            export SHELL_TYPE="host"
            export PATH=$QT_INSTALL_ROOT/gcc_64/bin:$PATH
            export LD_LIBRARY_PATH="${pkgs.lib.makeLibraryPath (libList pkgs)}:${pkgs.stdenv.cc.cc.lib}/lib:$QT_INSTALL_ROOT/gcc_64/lib:/run/opengl-driver/lib:/usr/lib:$LD_LIBRARY_PATH"
            echo "[quickrecord] Environment Loaded."
          '';
        };
      }
    );
}
