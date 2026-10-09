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

} // namespace qr
