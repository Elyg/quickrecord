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
- Runs only on X11 (Linux).

## Usage

| Action | Default hotkey |
| --- | --- |
| Start / stop recording | `Meta+Shift+R` |
| Take a screenshot | `Meta+Shift+S` |

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
quickrecord --quit
```

Screenshots are saved to `~/Pictures/quickrecord` and exports to `~/Videos/quickrecord`.
You can change both folders in Settings.

## Building

The toolchain comes from the nix flake (`direnv allow` or `nix develop`). Qt is
downloaded into `.sdk/`.

```sh
just init      # conan deps → configure → build → run
just build     # Debug build
just run       # build and start the tray app
just tests     # run unit tests
just package   # build the AppImage (bundles static ffmpeg + gifski)
```

Releases are tagged with `just release`, which triggers the CI build on `v*` tags.

## Development

Developed with assistance from [Claude Code](https://claude.com/claude-code). All changes
are reviewed, tested, and maintained by me.
