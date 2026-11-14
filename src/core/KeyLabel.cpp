#include "core/KeyLabel.h"

namespace qr
{

namespace
{
// The same symbols as on macOS (InputMonitorMac.mm), so recordings read alike.
QString symbol(const KeyPress& key)
{
    switch(key.special)
    {
        case SpecialKey::None: return {};
        case SpecialKey::Return: return QStringLiteral("↩");
        case SpecialKey::Tab: return QStringLiteral("⇥");
        case SpecialKey::Space: return QStringLiteral("Space");
        case SpecialKey::Backspace: return QStringLiteral("⌫");
        case SpecialKey::Delete: return QStringLiteral("⌦");
        case SpecialKey::Escape: return QStringLiteral("⎋");
        case SpecialKey::Left: return QStringLiteral("←");
        case SpecialKey::Right: return QStringLiteral("→");
        case SpecialKey::Up: return QStringLiteral("↑");
        case SpecialKey::Down: return QStringLiteral("↓");
        case SpecialKey::Home: return QStringLiteral("↖");
        case SpecialKey::End: return QStringLiteral("↘");
        case SpecialKey::PageUp: return QStringLiteral("⇞");
        case SpecialKey::PageDown: return QStringLiteral("⇟");
        case SpecialKey::Function:
            return key.functionKey > 0 ? QStringLiteral("F%1").arg(key.functionKey) : QString();
    }
    return {};
}
} // namespace

KeyLabel keyLabel(const KeyPress& key, MetaName metaName)
{
    const QString sym = symbol(key);
    if(!key.ctrl && !key.alt && !key.meta)
    {
        if(key.special == SpecialKey::Space)
            return {QStringLiteral(" "), true};
        if(sym.isEmpty() && !key.text.isEmpty() && key.text.at(0).isPrint())
            return {key.text, true};
    }

    const QString name = sym.isEmpty() ? key.base.toUpper() : sym;
    if(name.isEmpty() || !name.at(0).isPrint())
        return {};
    QString mods;
    if(key.ctrl)
        mods += QStringLiteral("Ctrl+");
    if(key.alt)
        mods += QStringLiteral("Alt+");
    if(key.shift)
        mods += QStringLiteral("Shift+");
    if(key.meta)
        mods += metaName == MetaName::Win ? QStringLiteral("Win+") : QStringLiteral("Super+");
    return {mods + name, false};
}

} // namespace qr
