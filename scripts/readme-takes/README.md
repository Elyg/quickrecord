# README takes (prototype)

Scripts that filmed a first scripted take (area picker → record → editor). The README's
`docs/media/demo.gif` was recorded by hand instead: ⌘⇧5 filming a second QuickRecord.
`qr-demo start [--fresh]` / `qr-demo stop` runs that second QuickRecord (step 1 below).
macOS only, run by hand; not wired into the justfile yet.

## How a take is made

1. **A demo QuickRecord next to yours.** `open -n -a QuickRecord` with
   `XDG_RUNTIME_DIR=/Users/Shared/qr-demo/run` (its own socket, so it doesn't talk to your
   running instance) and `CFFIXED_USER_HOME` / `HOME=/Users/Shared/qr-demo/home` (its own
   settings, history, Pictures and Movies, so your history stays out of the shots).
   `demo.conf` goes in `home/Library/Preferences/quickrecord/quickrecord.conf`: other
   hotkeys (⌃⌥⇧R/S/G), a 10 px draw width. `qr` sends it commands (`--toggle`,
   `--screenshot`, `--record-rect`, `--stop`, `--history`, `--edit`, `--quit`).
2. **Something to film.** `stage.sh` runs rega on its tutorial images in a Ghostty window
   (rega's `docs/clips/ghostty.conf`), moved to an AeroSpace workspace of its own.
   `front` brings it to the front with keyboard focus.
3. **Seed the history** (for history / contact-sheet shots): screenshots via
   `qr --screenshot` + a drag + Enter, recordings via `qr --record-rect … ; qr --stop`.
4. **Film** with `film start NAME SECONDS` / `film wait`: `screencapture -v` (cursor and
   clicks shown), fixed length. Not ffmpeg's avfoundation: while it captures, QuickRecord's
   own recording fails ("Nothing was recorded"). `screencapture -v` ignores SIGINT and
   drops the file on SIGTERM, hence the fixed length.
5. **Drive it**: `mouse` (glide, click, drag, circle; compile `mouse.swift`), `key`
   (System Events). `floater X Y W H` runs in the background and floats + places each new
   QuickRecord window, since AeroSpace would tile it beside the stage.
6. **Check** frames with `sheet MOV OUT.png t1 t2 …`, then `mkgif MOV OUT.gif "a-b c-d"`
   cuts segments, crops the menu bar, scales to 800 px and runs gifski (`GIFSKI=…` to
   point at one).

Needs Screen Recording and Accessibility for the terminal. The coordinates in a take are
screen points on a 1280×832 display.

## Lessons from the first takes

- The screenshot editor opens with the width box focused: typed digits land there.
- Its toolbar is one row when the window is wide; positions move with the window size.
- The editor keeps its zoom on resize (`0` / double-click fits).
- Browser keys sent to rega right after QuickRecord had focus got lost; opening rega on
  a list of files and stepping with `n` / `p` was reliable.
