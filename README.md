# QuickRecord

A simple screenshot and screen recorder that doesn't interrupt your flow.

QuickRecord sits in the system tray. Press a hotkey, pick an area, and get on with what
you were doing. The result is copied to the clipboard, ready to paste.

## Features

- **Screen recording** to GIF or MP4: pick a region, a window or the whole screen.
- **Screenshots** with simple drawing tools to annotate before saving.
- **Quick editor**: trim, crop and add a caption, then Save & Copy.
- **History** of past recordings, screenshots and exports.
- **Record the last area again** in one keystroke.
- Runs on Linux (X11 sessions only) and macOS 13+ (Apple Silicon).

## Usage

| Action | Default hotkey |
| --- | --- |
| Start / stop recording | `Meta+Shift+R` (macOS: `⌃⇧R`) |
| Take a screenshot | `Meta+Shift+S` (macOS: `⌃⇧S`) |
| Laser pointer on/off | `Meta+Shift+G` (macOS: `⌃⇧G`) |

The laser pointer (hotkey, the ✎ button on the record bar, or the tray menu) lets you drag
to circle or underline something; the stroke fades away by itself. While recording it covers
the recorded area, otherwise the screen under the mouse. Press Esc (or right-click) to turn it off.

In the area picker, drag a region (adjust it with the handles, then press Enter), click a
window, or Shift+click for the full screen. Press Tab, `S` or `R` to switch between
screenshot and recording mode. Enter with no selection reuses the last area.

You can also control a running instance from the command line:

```sh
quickrecord --toggle      # pick an area, or stop the current recording
quickrecord --last        # record the last used area again
quickrecord --screenshot  # pick an area for a screenshot
quickrecord --history     # open the history window
quickrecord --settings    # open settings
quickrecord --edit <id>   # open a recording or screenshot (e.g. qs-20261018-110900) in the editor
quickrecord --quit
```

Screenshots are saved to `~/Pictures/quickrecord` and exports to `~/Videos/quickrecord`
(`~/Movies/quickrecord` on macOS). You can change both folders in Settings.

### macOS

QuickRecord sits in the menu bar and the Dock: the Dock icon's menu has the same actions
(handy when a full menu bar hides the icon), and clicking it opens the history. It records
with ffmpeg's `avfoundation` input, so install ffmpeg first (`brew install ffmpeg` or
`nix profile install nixpkgs#ffmpeg`; add `gifski` for the best GIFs).

**Install or update:** download `QuickRecord-<version>-macos-arm64.dmg` from the
[releases](https://github.com/Elyg/quickrecord/releases), quit QuickRecord if it's running,
and drag the app to Applications (replace the old one). The builds aren't notarized, so
macOS blocks the first launch: open System Settings › Privacy & Security and click **Open
Anyway**, or run `xattr -dr com.apple.quarantine /Applications/QuickRecord.app` once.

On first start macOS asks for **Screen Recording** (and **Microphone** / **Input
Monitoring** if you turn on mic or keys & clicks recording). Allow them in System Settings ›
Privacy & Security, then restart QuickRecord. The builds are signed ad hoc, so macOS treats
each update as a new app: after updating, switch those permissions off and on again (or
remove and re-add QuickRecord) if recording shows only the wallpaper or keys stay empty.

The command-line options above work with the binary inside the bundle, e.g.
`/Applications/QuickRecord.app/Contents/MacOS/QuickRecord --toggle`. A recording covers one
display: an area that spans two displays is cut to the one under its centre.

**Raycast:** `just raycast` loads the extension in `raycast/` into Raycast (it stays there
after you stop the command). It adds Search Screenshots / Search Recordings (paste, copy,
open in the editor, show in Finder), Paste / Copy Last Screenshot, and Record, Record Last
Area, Take Screenshot and Show History. It expects the app in `/Applications`; change that
in the extension's preferences.

## Building

The toolchain comes from the nix flake (`direnv allow` or `nix develop`), on Linux x64
and macOS arm64. Qt is downloaded into `.sdk/`.

```sh
just init      # conan deps → configure → build → run
just build     # Debug build
just run       # build and start the tray app
just tests     # run unit tests
just package   # build the AppImage (bundles static ffmpeg + gifski) or, on macOS, the .dmg
```

Releases are tagged with `just release`, which triggers the CI build on `v*` tags.

## Development

Developed with assistance from [Claude Code](https://claude.com/claude-code). All changes
are reviewed, tested, and maintained by me.
