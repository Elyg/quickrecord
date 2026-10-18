#pragma once

#include "core/FfmpegArgs.h"

#include <QProcessEnvironment>
#include <QString>

namespace qr
{

// Locates the external encoders. Candidates are the binaries bundled next to the app
// (AppImage usr/bin) followed by every ffmpeg on PATH (plus Homebrew's prefixes on macOS,
// which apps started from Finder don't have on PATH); each is probed once for the
// capabilities we need, because builds differ (the static bundle has x11grab but no
// PulseAudio; some distro/nix builds lack x11grab).
class Tools
{
public:
    struct Capture
    {
        QString ffmpeg;
        AudioInput audio = AudioInput::None;
        QString warning; // non-empty when we had to degrade (e.g. ALSA instead of Pulse)
    };

    // ffmpeg for x11grab / avfoundation (+ microphone when wantMic). Empty ffmpeg on failure; see error.
    static Capture capture(bool wantMic, QString* error);
    // ffmpeg with libx264 for exports/thumbnails. Empty if none.
    static QString exportFfmpeg();
    // gifski, or empty (callers fall back to ffmpeg's palettegen).
    static QString gifski();

    // Environment for tool processes: drops our bundled library/plugin paths so host
    // binaries don't load the AppImage's glibc or the dev shell's nix libs.
    static QProcessEnvironment processEnv();
};

} // namespace qr
