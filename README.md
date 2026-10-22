# QuickRecord

A simple screenshot and screen recorder that doesn't interrupt your flow.

QuickRecord sits in the system tray. Press a hotkey, pick an area, and get on with what
you were doing. The result is copied to the clipboard, ready to paste.

## Features

- **Screen recording** to GIF or MP4: pick a region, a window or the whole screen.
- **Screenshots** with simple drawing tools to annotate before saving.
- **Quick editor**: trim, crop and add a caption, then Copy.
- **History** of past recordings and screenshots, with their edited versions.
- **Contact sheets**: several screenshots on one image, with text in the corners.
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
screenshot and recording mode. Enter with no selection reuses the last area. `H` shows the
full list of shortcuts (it stays on until you press `H` again).

The editors have three buttons: **Close** (Esc) keeps your edits for next time, **Save &
Close** (Ctrl+S) writes the edited file, and **Copy** (Ctrl+C) saves it and copies it to the
clipboard. Each item has at most one edited file per format, overwritten on every save:
`qs-….png` gets `qs-…-edited.png`, `qr-….mp4` gets `qr-…-edited.gif` and/or
`qr-…-edited.mp4`. With no edits nothing is written and Copy copies the original.

In History, select several items (Shift+click for a range, Ctrl+click — ⌘+click on macOS —
for single ones) to copy (Ctrl+C / ⌘C) or delete (Delete) them together. Selecting two or more screenshots shows **Make sheet**: a contact sheet with
the tiles in a grid (drag one onto another to reorder), and text in the six corners of the
sheet's header and footer, of every tile, or of one tile. `{n}`, `{name}`, `{date}`, `{time}`
and `{size}` are filled in. The sheet is saved as `qsheet-….png` and can be reopened and
changed later.

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

For scripts, a running instance also records without any UI. Each command waits for the
answer and prints it (`ok …`, exit 0; `error …`, exit 1):

```sh
quickrecord --record-rect 100,80,1280,720 --keys   # record this area (logical pixels), keys and clicks too
quickrecord --stop                                 # → ok <id>\t<master.mp4>, once written
quickrecord --export <id> --out clip.mp4 --keys-corner bottom-right   # MP4, keys and clicks burned in
```

Screenshots are saved to `~/Pictures/quickrecord` and recordings to `~/Videos/quickrecord`
(`~/Movies/quickrecord` on macOS), each next to its edited versions. You can change both
folders in Settings. The history index, thumbnails and key logs stay in the app's data folder
(`~/.local/share/quickrecord`, `~/Library/Application Support/quickrecord` on macOS).

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
Privacy & Security, then restart QuickRecord. Release builds are signed with the project's
own (self-signed) certificate, so these permissions stay granted across updates. If
recording ever shows only the wallpaper, reset them and allow them again:
`tccutil reset ScreenCapture com.etitas.quickrecord` (likewise `ListenEvent`, `Microphone`).

`just mac-signing-setup` creates that certificate once and stores it only in the GitHub
Actions secrets the release workflow signs with (no copy stays on your Mac: anyone holding it
could sign a program macOS would treat as QuickRecord). Local `just package` builds are
signed ad hoc.

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
