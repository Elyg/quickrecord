#include "app/GlobalHotkey.h"

#include "app/X11.h"
#include "core/Hotkey.h"

#include <QCoreApplication>
#include <cstdlib>
#include <xcb/xcb.h>
#include <xcb/xcb_keysyms.h>

namespace qr
{

namespace
{
// Grab every Lock/NumLock combination so the hotkey works regardless of those states.
constexpr uint16_t kIgnoredVariants[] = {0, kModLock, kModNumLock, kModLock | kModNumLock};
} // namespace

GlobalHotkey::GlobalHotkey(QObject* parent)
    : QObject(parent)
{
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
    xcb_connection_t* c = x11::connection();
    if(!c)
    {
        *error = "Not running on X11.";
        return false;
    }
    const auto hk = toX11Hotkey(seq);
    if(!hk)
    {
        *error = QStringLiteral("Unsupported key in shortcut \"%1\".").arg(seq.toString());
        return false;
    }

    xcb_key_symbols_t* syms = xcb_key_symbols_alloc(c);
    xcb_keycode_t* codes = xcb_key_symbols_get_keycode(syms, hk->keysym);
    const uint8_t keycode = (codes && codes[0] != XCB_NO_SYMBOL) ? codes[0] : 0;
    std::free(codes);
    xcb_key_symbols_free(syms);
    if(!keycode)
    {
        *error = QStringLiteral("No key on this keyboard produces \"%1\".").arg(seq.toString());
        return false;
    }

    const xcb_window_t root = x11::rootWindow();
    bool ok = true;
    for(uint16_t extra : kIgnoredVariants)
    {
        xcb_void_cookie_t ck = xcb_grab_key_checked(c, 1, root, hk->modifiers | extra, keycode, XCB_GRAB_MODE_ASYNC,
                                                    XCB_GRAB_MODE_ASYNC);
        if(xcb_generic_error_t* err = xcb_request_check(c, ck))
        {
            ok = false;
            std::free(err);
        }
    }
    m_keycode = keycode;
    m_modifiers = hk->modifiers;
    if(!ok)
    {
        clear();
        *error = QStringLiteral("\"%1\" is already taken by another application.").arg(seq.toString(QKeySequence::NativeText));
        return false;
    }
    return true;
}

void GlobalHotkey::clear()
{
    if(!m_keycode)
        return;
    if(xcb_connection_t* c = x11::connection())
    {
        for(uint16_t extra : kIgnoredVariants)
            xcb_ungrab_key(c, m_keycode, x11::rootWindow(), m_modifiers | extra);
        xcb_flush(c);
    }
    m_keycode = 0;
    m_modifiers = 0;
}

bool GlobalHotkey::nativeEventFilter(const QByteArray& eventType, void* message, qintptr*)
{
    if(!m_keycode || eventType != "xcb_generic_event_t")
        return false;
    auto* ev = static_cast<xcb_generic_event_t*>(message);
    if((ev->response_type & ~0x80) != XCB_KEY_PRESS)
        return false;
    auto* kp = reinterpret_cast<xcb_key_press_event_t*>(ev);
    const uint16_t state = kp->state & ~(kModLock | kModNumLock);
    if(kp->detail != m_keycode || state != m_modifiers)
        return false;

    // Holding the key auto-repeats press events (first repeat after ~500ms, then every
    // ~30ms). Only a press after a quiet gap counts as a new activation.
    const bool fresh = !m_lastPress.isValid() || m_lastPress.elapsed() > 600;
    m_lastPress.start();
    if(fresh)
        emit activated();
    return true;
}

} // namespace qr
