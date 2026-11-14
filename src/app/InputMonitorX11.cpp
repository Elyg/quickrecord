#include "app/InputMonitor.h"

#include "app/Platform.h"
#include "app/X11.h"
#include "core/Geometry.h"
#include "core/KeyLabel.h"

#include <QSocketNotifier>

#include <xcb/xcb.h>
#include <xcb/xinput.h>
// xkb.h names a field `explicit`.
#define explicit explicit_
#include <xcb/xkb.h>
#undef explicit
#include <xkbcommon/xkbcommon-x11.h>
#include <xkbcommon/xkbcommon.h>

#include <cstdlib>

namespace qr
{

namespace
{
SpecialKey specialKey(xkb_keysym_t sym, int* functionKey)
{
    if(sym >= XKB_KEY_F1 && sym <= XKB_KEY_F24)
    {
        *functionKey = int(sym - XKB_KEY_F1) + 1;
        return SpecialKey::Function;
    }
    switch(sym)
    {
        case XKB_KEY_Return:
        case XKB_KEY_KP_Enter: return SpecialKey::Return;
        case XKB_KEY_Tab:
        case XKB_KEY_ISO_Left_Tab: return SpecialKey::Tab; // Shift+Tab
        case XKB_KEY_space: return SpecialKey::Space;
        case XKB_KEY_BackSpace: return SpecialKey::Backspace;
        case XKB_KEY_Delete:
        case XKB_KEY_KP_Delete: return SpecialKey::Delete;
        case XKB_KEY_Escape: return SpecialKey::Escape;
        case XKB_KEY_Left:
        case XKB_KEY_KP_Left: return SpecialKey::Left;
        case XKB_KEY_Right:
        case XKB_KEY_KP_Right: return SpecialKey::Right;
        case XKB_KEY_Up:
        case XKB_KEY_KP_Up: return SpecialKey::Up;
        case XKB_KEY_Down:
        case XKB_KEY_KP_Down: return SpecialKey::Down;
        case XKB_KEY_Home:
        case XKB_KEY_KP_Home: return SpecialKey::Home;
        case XKB_KEY_End:
        case XKB_KEY_KP_End: return SpecialKey::End;
        case XKB_KEY_Prior:
        case XKB_KEY_KP_Prior: return SpecialKey::PageUp;
        case XKB_KEY_Next:
        case XKB_KEY_KP_Next: return SpecialKey::PageDown;
        default: return SpecialKey::None;
    }
}

// XKB events share one X event code; the XKB type is the second byte.
union XkbEvent
{
    struct
    {
        uint8_t response_type;
        uint8_t xkbType;
        uint16_t sequence;
        xcb_timestamp_t time;
        uint8_t deviceID;
    } any;
    xcb_xkb_new_keyboard_notify_event_t newKeyboard;
    xcb_xkb_map_notify_event_t map;
    xcb_xkb_state_notify_event_t state;
};
} // namespace

// Everything lives on a connection of our own: selecting XInput2 events on the root window
// replaces what that client selected there before, which on Qt's connection would break
// Qt's own device tracking.
struct InputMonitor::Private
{
    xcb_connection_t* conn = nullptr;
    xcb_window_t root = 0;
    uint8_t xiOpcode = 0;
    uint8_t xkbFirstEvent = 0;
    int32_t keyboard = -1;
    xkb_context* context = nullptr;
    xkb_keymap* keymap = nullptr;
    xkb_state* state = nullptr;
    std::unique_ptr<QSocketNotifier> notifier;

    ~Private() { close(); }

    void close()
    {
        notifier.reset();
        xkb_state_unref(state);
        xkb_keymap_unref(keymap);
        xkb_context_unref(context);
        state = nullptr;
        keymap = nullptr;
        context = nullptr;
        if(conn)
            xcb_disconnect(conn);
        conn = nullptr;
    }

    // The keymap and the current modifiers / layout, from the server.
    bool loadKeymap()
    {
        xkb_keymap* newKeymap = xkb_x11_keymap_new_from_device(context, conn, keyboard, XKB_KEYMAP_COMPILE_NO_FLAGS);
        if(!newKeymap)
            return false;
        xkb_state* newState = xkb_x11_state_new_from_device(newKeymap, conn, keyboard);
        if(!newState)
        {
            xkb_keymap_unref(newKeymap);
            return false;
        }
        xkb_state_unref(state);
        xkb_keymap_unref(keymap);
        keymap = newKeymap;
        state = newState;
        return true;
    }

    // State changes come on this connection in order with the raw key events, so the state
    // when a key's event is handled is the one it was pressed in.
    void selectXkbEvents()
    {
        constexpr uint16_t events = XCB_XKB_EVENT_TYPE_NEW_KEYBOARD_NOTIFY | XCB_XKB_EVENT_TYPE_MAP_NOTIFY |
                                    XCB_XKB_EVENT_TYPE_STATE_NOTIFY;
        constexpr uint16_t mapParts = XCB_XKB_MAP_PART_KEY_TYPES | XCB_XKB_MAP_PART_KEY_SYMS |
                                      XCB_XKB_MAP_PART_MODIFIER_MAP | XCB_XKB_MAP_PART_EXPLICIT_COMPONENTS |
                                      XCB_XKB_MAP_PART_KEY_ACTIONS | XCB_XKB_MAP_PART_VIRTUAL_MODS |
                                      XCB_XKB_MAP_PART_VIRTUAL_MOD_MAP;
        constexpr uint16_t stateParts = XCB_XKB_STATE_PART_MODIFIER_BASE | XCB_XKB_STATE_PART_MODIFIER_LATCH |
                                        XCB_XKB_STATE_PART_MODIFIER_LOCK | XCB_XKB_STATE_PART_GROUP_BASE |
                                        XCB_XKB_STATE_PART_GROUP_LATCH | XCB_XKB_STATE_PART_GROUP_LOCK;
        xcb_xkb_select_events_details_t details = {};
        details.affectNewKeyboard = XCB_XKB_NKN_DETAIL_KEYCODES;
        details.newKeyboardDetails = XCB_XKB_NKN_DETAIL_KEYCODES;
        details.affectState = stateParts;
        details.stateDetails = stateParts;
        xcb_xkb_select_events_aux(conn, xcb_xkb_device_spec_t(keyboard), events, 0, 0, mapParts, mapParts, &details);
    }

    void selectRawEvents()
    {
        struct
        {
            xcb_input_event_mask_t head;
            uint32_t mask;
        } select = {};
        select.head.deviceid = XCB_INPUT_DEVICE_ALL_MASTER;
        select.head.mask_len = 1;
        select.mask = XCB_INPUT_XI_EVENT_MASK_RAW_KEY_PRESS | XCB_INPUT_XI_EVENT_MASK_RAW_BUTTON_PRESS;
        xcb_input_xi_select_events(conn, root, 1, &select.head);
    }

    // One event from the connection: keyboard state updates, or a key / button to report.
    void handle(const xcb_generic_event_t* event, InputMonitor* q);
};

void InputMonitor::Private::handle(const xcb_generic_event_t* event, InputMonitor* q)
{
    const uint8_t type = event->response_type & 0x7f;

    if(type == xkbFirstEvent)
    {
        const auto* xkb = reinterpret_cast<const XkbEvent*>(event);
        if(xkb->any.deviceID != keyboard)
            return;
        switch(xkb->any.xkbType)
        {
            case XCB_XKB_NEW_KEYBOARD_NOTIFY:
                if(xkb->newKeyboard.changed & XCB_XKB_NKN_DETAIL_KEYCODES)
                    loadKeymap();
                break;
            case XCB_XKB_MAP_NOTIFY: loadKeymap(); break;
            case XCB_XKB_STATE_NOTIFY:
                xkb_state_update_mask(state, xkb->state.baseMods, xkb->state.latchedMods, xkb->state.lockedMods,
                                      xkb->state.baseGroup, xkb->state.latchedGroup, xkb->state.lockedGroup);
                break;
            default: break;
        }
        return;
    }

    if(type != XCB_GE_GENERIC)
        return;
    const auto* ge = reinterpret_cast<const xcb_ge_generic_event_t*>(event);
    if(ge->extension != xiOpcode)
        return;

    if(ge->event_type == XCB_INPUT_RAW_BUTTON_PRESS)
    {
        const auto* raw = reinterpret_cast<const xcb_input_raw_button_press_event_t*>(event);
        if(raw->detail >= 4 && raw->detail <= 7)
            return; // the wheel
        xcb_query_pointer_reply_t* pointer = xcb_query_pointer_reply(conn, xcb_query_pointer(conn, root), nullptr);
        if(!pointer)
            return;
        const QPoint physical(pointer->root_x, pointer->root_y);
        std::free(pointer);
        const QList<ScreenInfo> screens = platform::screens();
        const int si = screenAtPhysical(screens, physical);
        if(si >= 0)
            emit q->mousePressed(toLogical(QRect(physical, QSize(1, 1)), screens[si]).topLeft());
        return;
    }

    if(ge->event_type == XCB_INPUT_RAW_KEY_PRESS)
    {
        const auto* raw = reinterpret_cast<const xcb_input_raw_key_press_event_t*>(event);
        if(raw->flags & XCB_INPUT_KEY_EVENT_FLAGS_KEY_REPEAT)
            return;
        const xkb_keycode_t code = raw->detail;

        KeyPress key;
        key.special = specialKey(xkb_state_key_get_one_sym(state, code), &key.functionKey);
        char text[16] = {};
        xkb_state_key_get_utf8(state, code, text, sizeof text);
        key.text = QString::fromUtf8(text);
        const xkb_keysym_t* syms = nullptr;
        if(xkb_keymap_key_get_syms_by_level(keymap, code, xkb_state_key_get_layout(state, code), 0, &syms) > 0)
        {
            if(const uint32_t ch = xkb_keysym_to_utf32(syms[0]))
                key.base = QString::fromUcs4(reinterpret_cast<const char32_t*>(&ch), 1);
        }
        const auto active = [&](const char* mod) {
            return xkb_state_mod_name_is_active(state, mod, XKB_STATE_MODS_EFFECTIVE) > 0;
        };
        key.ctrl = active(XKB_MOD_NAME_CTRL);
        key.alt = active(XKB_MOD_NAME_ALT);
        key.shift = active(XKB_MOD_NAME_SHIFT);
        key.meta = active(XKB_MOD_NAME_LOGO);

        const KeyLabel label = keyLabel(key, MetaName::Super);
        if(!label.label.isEmpty())
            emit q->keyPressed(label.label, label.typed);
    }
}

InputMonitor::InputMonitor(QObject* parent)
    : QObject(parent)
    , d(std::make_unique<Private>())
{
}

InputMonitor::~InputMonitor() = default;

bool InputMonitor::supported()
{
    return true;
}

bool InputMonitor::start(QString* error)
{
    if(d->conn)
        return true;
    const auto fail = [&](const QString& why) {
        d->close();
        *error = "Could not listen to the keyboard and mouse: " + why;
        return false;
    };

    int screen = 0;
    d->conn = xcb_connect(x11::displayName().toUtf8().constData(), &screen);
    if(xcb_connection_has_error(d->conn))
        return fail("no connection to the X server.");
    xcb_screen_iterator_t it = xcb_setup_roots_iterator(xcb_get_setup(d->conn));
    for(int i = 0; i < screen && it.rem; ++i)
        xcb_screen_next(&it);
    if(!it.rem)
        return fail("no X screen.");
    d->root = it.data->root;

    // 2.1+: raw events arrive even while another client has grabbed the keyboard or mouse.
    const xcb_query_extension_reply_t* xi = xcb_get_extension_data(d->conn, &xcb_input_id);
    if(!xi || !xi->present)
        return fail("the X server has no XInput extension.");
    d->xiOpcode = xi->major_opcode;
    xcb_input_xi_query_version_reply_t* version =
        xcb_input_xi_query_version_reply(d->conn, xcb_input_xi_query_version(d->conn, 2, 2), nullptr);
    const bool xi2 = version && version->major_version >= 2;
    std::free(version);
    if(!xi2)
        return fail("the X server's XInput is older than 2.0.");

    if(!xkb_x11_setup_xkb_extension(d->conn, XKB_X11_MIN_MAJOR_XKB_VERSION, XKB_X11_MIN_MINOR_XKB_VERSION,
                                    XKB_X11_SETUP_XKB_EXTENSION_NO_FLAGS, nullptr, nullptr, &d->xkbFirstEvent,
                                    nullptr))
        return fail("the X server has no XKB extension.");
    d->keyboard = xkb_x11_get_core_keyboard_device_id(d->conn);
    d->context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    if(d->keyboard < 0 || !d->context || !d->loadKeymap())
        return fail("could not read the keyboard layout.");

    d->selectXkbEvents();
    d->selectRawEvents();
    xcb_flush(d->conn);

    d->notifier = std::make_unique<QSocketNotifier>(xcb_get_file_descriptor(d->conn), QSocketNotifier::Read);
    connect(d->notifier.get(), &QSocketNotifier::activated, this, [this] {
        while(d->conn)
        {
            xcb_generic_event_t* event = xcb_poll_for_event(d->conn);
            if(!event)
                break;
            d->handle(event, this);
            std::free(event);
        }
        if(d->conn && xcb_connection_has_error(d->conn))
            d->close(); // the X server went away; nothing more to log
    });
    return true;
}

void InputMonitor::stop()
{
    d->close();
}

} // namespace qr
