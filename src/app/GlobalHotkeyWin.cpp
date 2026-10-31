#include "app/GlobalHotkey.h"

#include "core/Hotkey.h"

#include <QCoreApplication>

#include <qt_windows.h>

namespace qr
{

GlobalHotkey::GlobalHotkey(QObject* parent)
    : QObject(parent)
{
    static int nextId = 1;
    m_id = nextId++;
    QCoreApplication::instance()->installNativeEventFilter(this);
}

GlobalHotkey::~GlobalHotkey()
{
    clear();
    QCoreApplication::instance()->removeNativeEventFilter(this);
}

bool GlobalHotkey::setShortcut(const QKeySequence& seq, QString* error)
{
    clear();
    const auto hk = toWinHotkey(seq);
    if(!hk)
    {
        *error = QStringLiteral("Unsupported key in shortcut \"%1\".").arg(seq.toString());
        return false;
    }
    // No window: WM_HOTKEY arrives on this thread's queue. MOD_NOREPEAT: once per press.
    if(!RegisterHotKey(nullptr, m_id, hk->modifiers | MOD_NOREPEAT, hk->virtualKey))
    {
        *error = QStringLiteral("\"%1\" is already taken by another application.").arg(seq.toString(QKeySequence::NativeText));
        return false;
    }
    m_registered = true;
    return true;
}

void GlobalHotkey::clear()
{
    if(!m_registered)
        return;
    UnregisterHotKey(nullptr, m_id);
    m_registered = false;
}

bool GlobalHotkey::nativeEventFilter(const QByteArray&, void* message, qintptr*)
{
    const MSG* msg = static_cast<MSG*>(message);
    if(msg->message != WM_HOTKEY || int(msg->wParam) != m_id)
        return false;
    emit activated();
    return true;
}

} // namespace qr
