#include "app/GlobalHotkey.h"

#include "core/Hotkey.h"

#include <Carbon/Carbon.h>
#include <QCoreApplication>
#include <QHash>

namespace qr
{

namespace
{
constexpr OSType kSignature = 'QRhk';

QHash<uint32_t, GlobalHotkey*>& registry()
{
    static QHash<uint32_t, GlobalHotkey*> hotkeys;
    return hotkeys;
}

// Carbon hot keys fire once per press (no auto-repeat), so no debouncing is needed here.
OSStatus onHotKey(EventHandlerCallRef, EventRef event, void*)
{
    EventHotKeyID id;
    if(GetEventParameter(event, kEventParamDirectObject, typeEventHotKeyID, nullptr, sizeof(id), nullptr, &id) != noErr ||
       id.signature != kSignature)
        return eventNotHandledErr;
    if(GlobalHotkey* hotkey = registry().value(id.id))
        emit hotkey->activated();
    return noErr;
}

void installHandler()
{
    static bool installed = false;
    if(installed)
        return;
    const EventTypeSpec spec{kEventClassKeyboard, kEventHotKeyPressed};
    installed = InstallApplicationEventHandler(&onHotKey, 1, &spec, nullptr, nullptr) == noErr;
}
} // namespace

GlobalHotkey::GlobalHotkey(QObject* parent)
    : QObject(parent)
{
    static uint32_t nextId = 1;
    m_id = nextId++;
    registry().insert(m_id, this);
}

GlobalHotkey::~GlobalHotkey()
{
    clear();
    registry().remove(m_id);
}

bool GlobalHotkey::setShortcut(const QKeySequence& seq, QString* error)
{
    clear();
    const auto hk = toMacHotkey(seq);
    if(!hk)
    {
        *error = QStringLiteral("Unsupported key in shortcut \"%1\".").arg(seq.toString());
        return false;
    }
    installHandler();
    EventHotKeyRef ref = nullptr;
    const OSStatus st = RegisterEventHotKey(hk->keyCode, hk->modifiers, EventHotKeyID{kSignature, m_id},
                                            GetApplicationEventTarget(), 0, &ref);
    if(st != noErr || !ref)
    {
        *error = QStringLiteral("\"%1\" is already taken by another application.").arg(seq.toString(QKeySequence::NativeText));
        return false;
    }
    m_ref = ref;
    return true;
}

void GlobalHotkey::clear()
{
    if(!m_ref)
        return;
    UnregisterEventHotKey(static_cast<EventHotKeyRef>(m_ref));
    m_ref = nullptr;
}

bool GlobalHotkey::nativeEventFilter(const QByteArray&, void*, qintptr*)
{
    return false;
}

} // namespace qr
