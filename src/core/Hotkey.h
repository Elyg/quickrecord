#pragma once

#include <QKeySequence>
#include <cstdint>
#include <optional>

namespace qr
{

// X11 modifier masks (X.h ShiftMask / ControlMask / Mod1Mask / Mod4Mask).
inline constexpr uint16_t kModShift = 1 << 0;
inline constexpr uint16_t kModLock = 1 << 1;
inline constexpr uint16_t kModControl = 1 << 2;
inline constexpr uint16_t kModAlt = 1 << 3;  // Mod1
inline constexpr uint16_t kModNumLock = 1 << 4; // Mod2
inline constexpr uint16_t kModSuper = 1 << 6;   // Mod4

struct X11Hotkey
{
    uint32_t keysym = 0;
    uint16_t modifiers = 0;
};

// Translates the first chord of a Qt key sequence ("Meta+Shift+R") into an X keysym plus
// modifier mask suitable for XGrabKey. Returns nullopt for keys we don't map.
std::optional<X11Hotkey> toX11Hotkey(const QKeySequence& seq);

// Carbon modifier masks (Events.h cmdKey / shiftKey / optionKey / controlKey).
inline constexpr uint32_t kMacCmd = 1 << 8;
inline constexpr uint32_t kMacShift = 1 << 9;
inline constexpr uint32_t kMacOption = 1 << 11;
inline constexpr uint32_t kMacControl = 1 << 12;

struct MacHotkey
{
    uint32_t keyCode = 0; // kVK_* virtual key code (ANSI layout position)
    uint32_t modifiers = 0;
};

// Same for RegisterEventHotKey. As in Qt on macOS, Ctrl means ⌘ and Meta means ⌃.
// Print / ScrollLock / Pause map to F13 / F14 / F15, which sit in their place on Mac keyboards.
std::optional<MacHotkey> toMacHotkey(const QKeySequence& seq);

} // namespace qr
