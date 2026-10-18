#include "core/Hotkey.h"

namespace qr
{

namespace
{
std::optional<uint32_t> keysymFor(Qt::Key key)
{
    if(key >= Qt::Key_A && key <= Qt::Key_Z)
        return uint32_t('a' + (key - Qt::Key_A)); // lowercase latin keysyms
    if(key >= Qt::Key_0 && key <= Qt::Key_9)
        return uint32_t('0' + (key - Qt::Key_0));
    if(key >= Qt::Key_F1 && key <= Qt::Key_F35)
        return uint32_t(0xffbe + (key - Qt::Key_F1));
    switch(key)
    {
        case Qt::Key_Print: return 0xff61;
        case Qt::Key_Pause: return 0xff13;
        case Qt::Key_ScrollLock: return 0xff14;
        case Qt::Key_Space: return 0x20;
        case Qt::Key_Insert: return 0xff63;
        case Qt::Key_Delete: return 0xffff;
        case Qt::Key_Home: return 0xff50;
        case Qt::Key_End: return 0xff57;
        case Qt::Key_PageUp: return 0xff55;
        case Qt::Key_PageDown: return 0xff56;
        default: return std::nullopt;
    }
}

std::optional<uint32_t> macKeyCodeFor(Qt::Key key)
{
    // kVK_ANSI_A … kVK_ANSI_Z and kVK_ANSI_0 … kVK_ANSI_9 aren't in alphabetical order.
    static constexpr uint8_t kLetters[26] = {0x00, 0x0B, 0x08, 0x02, 0x0E, 0x03, 0x05, 0x04, 0x22,
                                             0x26, 0x28, 0x25, 0x2E, 0x2D, 0x1F, 0x23, 0x0C, 0x0F,
                                             0x01, 0x11, 0x20, 0x09, 0x0D, 0x07, 0x10, 0x06};
    static constexpr uint8_t kDigits[10] = {0x1D, 0x12, 0x13, 0x14, 0x15, 0x17, 0x16, 0x1A, 0x1C, 0x19};
    static constexpr uint8_t kFKeys[20] = {0x7A, 0x78, 0x63, 0x76, 0x60, 0x61, 0x62, 0x64, 0x65, 0x6D,
                                           0x67, 0x6F, 0x69, 0x6B, 0x71, 0x6A, 0x40, 0x4F, 0x50, 0x5A};
    if(key >= Qt::Key_A && key <= Qt::Key_Z)
        return kLetters[key - Qt::Key_A];
    if(key >= Qt::Key_0 && key <= Qt::Key_9)
        return kDigits[key - Qt::Key_0];
    if(key >= Qt::Key_F1 && key <= Qt::Key_F20)
        return kFKeys[key - Qt::Key_F1];
    switch(key)
    {
        case Qt::Key_Print: return 0x69;      // F13
        case Qt::Key_ScrollLock: return 0x6B; // F14
        case Qt::Key_Pause: return 0x71;      // F15
        case Qt::Key_Space: return 0x31;
        case Qt::Key_Insert: return 0x72; // Help
        case Qt::Key_Delete: return 0x75; // forward delete
        case Qt::Key_Home: return 0x73;
        case Qt::Key_End: return 0x77;
        case Qt::Key_PageUp: return 0x74;
        case Qt::Key_PageDown: return 0x79;
        default: return std::nullopt;
    }
}
} // namespace

std::optional<X11Hotkey> toX11Hotkey(const QKeySequence& seq)
{
    if(seq.isEmpty())
        return std::nullopt;
    const QKeyCombination combo = seq[0];
    const auto sym = keysymFor(combo.key());
    if(!sym)
        return std::nullopt;

    X11Hotkey hk;
    hk.keysym = *sym;
    const Qt::KeyboardModifiers mods = combo.keyboardModifiers();
    if(mods & Qt::ShiftModifier)
        hk.modifiers |= kModShift;
    if(mods & Qt::ControlModifier)
        hk.modifiers |= kModControl;
    if(mods & Qt::AltModifier)
        hk.modifiers |= kModAlt;
    if(mods & Qt::MetaModifier)
        hk.modifiers |= kModSuper;
    return hk;
}

std::optional<MacHotkey> toMacHotkey(const QKeySequence& seq)
{
    if(seq.isEmpty())
        return std::nullopt;
    const QKeyCombination combo = seq[0];
    const auto code = macKeyCodeFor(combo.key());
    if(!code)
        return std::nullopt;

    MacHotkey hk;
    hk.keyCode = *code;
    const Qt::KeyboardModifiers mods = combo.keyboardModifiers();
    if(mods & Qt::ShiftModifier)
        hk.modifiers |= kMacShift;
    if(mods & Qt::ControlModifier)
        hk.modifiers |= kMacCmd;
    if(mods & Qt::AltModifier)
        hk.modifiers |= kMacOption;
    if(mods & Qt::MetaModifier)
        hk.modifiers |= kMacControl;
    return hk;
}

} // namespace qr
