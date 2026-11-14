#pragma once

#include <QString>

namespace qr
{

// Keys shown as a symbol rather than their character.
enum class SpecialKey
{
    None,
    Return,
    Tab,
    Space,
    Backspace,
    Delete,
    Escape,
    Left,
    Right,
    Up,
    Down,
    Home,
    End,
    PageUp,
    PageDown,
    Function, // F1…F24, see KeyPress::functionKey
};

// One key press as the X11 and Windows input monitors see it (macOS has its own rules).
struct KeyPress
{
    SpecialKey special = SpecialKey::None;
    int functionKey = 0; // 1…24 when special is Function
    QString text;        // what it types, with Shift / AltGr / Caps Lock applied ("A", "€")
    QString base;        // its character without any modifier ("a"), for shortcut labels
    bool ctrl = false;   // not set for AltGr, which only picks the character
    bool alt = false;
    bool shift = false;
    bool meta = false;   // Super / the Windows key
};

// How the Super / Windows key is spelled in shortcut labels.
enum class MetaName
{
    Super,
    Win,
};

struct KeyLabel
{
    QString label; // the typed text, or e.g. "Ctrl+Shift+R" / "↩"; empty: show nothing
    bool typed = false;
};

// Typing (no Ctrl, Alt or Meta: Shift and AltGr only choose the character) gives the text;
// anything else gives the shortcut, modifiers first. A modifier pressed alone gives nothing.
KeyLabel keyLabel(const KeyPress& key, MetaName metaName);

} // namespace qr
