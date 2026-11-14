#include "app/InputMonitor.h"

#include "app/Platform.h"
#include "core/Geometry.h"
#include "core/KeyLabel.h"

#include <QMetaObject>
#include <QString>

#include <future>
#include <thread>
#include <unordered_set>

#include <windows.h>

namespace qr
{

namespace
{
// Only touched on the hook thread, between start() and stop() (which joins it).
InputMonitor* g_monitor = nullptr;
std::unordered_set<DWORD> g_held; // keys down now: a key-down for one of these is auto-repeat
bool g_capsLock = false;

bool isDown(int vk)
{
    return g_held.count(DWORD(vk)) || (GetAsyncKeyState(vk) & 0x8000);
}

SpecialKey specialKey(DWORD vk, int* functionKey)
{
    if(vk >= VK_F1 && vk <= VK_F24)
    {
        *functionKey = int(vk - VK_F1) + 1;
        return SpecialKey::Function;
    }
    switch(vk)
    {
        case VK_RETURN: return SpecialKey::Return; // the keypad's Enter too
        case VK_TAB: return SpecialKey::Tab;
        case VK_SPACE: return SpecialKey::Space;
        case VK_BACK: return SpecialKey::Backspace;
        case VK_DELETE: return SpecialKey::Delete;
        case VK_ESCAPE: return SpecialKey::Escape;
        case VK_LEFT: return SpecialKey::Left;
        case VK_RIGHT: return SpecialKey::Right;
        case VK_UP: return SpecialKey::Up;
        case VK_DOWN: return SpecialKey::Down;
        case VK_HOME: return SpecialKey::Home;
        case VK_END: return SpecialKey::End;
        case VK_PRIOR: return SpecialKey::PageUp;
        case VK_NEXT: return SpecialKey::PageDown;
        default: return SpecialKey::None;
    }
}

// The key's character in the layout of the window being typed into. Flag 4 keeps Windows'
// own dead-key state as it is, so accents still work for the user while we look. Returns
// ToUnicodeEx's count: negative for a dead key, whose own (spacing) character is in `out`.
int layoutText(const KBDLLHOOKSTRUCT& k, const BYTE* state, QString* out)
{
    const HKL layout = GetKeyboardLayout(GetWindowThreadProcessId(GetForegroundWindow(), nullptr));
    wchar_t chars[8];
    const int n = ToUnicodeEx(k.vkCode, k.scanCode, state, chars, int(std::size(chars)), 0x4, layout);
    *out = n == 0 ? QString() : QString::fromWCharArray(chars, n < 0 ? 1 : n);
    return n;
}

void onKeyDown(const KBDLLHOOKSTRUCT& k)
{
    if(!g_held.insert(k.vkCode).second)
        return; // auto-repeat
    if(k.vkCode == VK_CAPITAL)
        g_capsLock = !g_capsLock;

    // AltGr arrives as Right Alt with a Left Ctrl Windows adds: it picks characters, it isn't
    // a shortcut.
    const bool altGr = isDown(VK_RMENU) && isDown(VK_LCONTROL);
    KeyPress key;
    key.ctrl = (isDown(VK_LCONTROL) && !altGr) || isDown(VK_RCONTROL);
    key.alt = isDown(VK_LMENU) || (isDown(VK_RMENU) && !altGr);
    key.shift = isDown(VK_LSHIFT) || isDown(VK_RSHIFT);
    key.meta = isDown(VK_LWIN) || isDown(VK_RWIN);
    key.special = specialKey(k.vkCode, &key.functionKey);

    BYTE state[256] = {};
    if(key.shift)
        state[VK_SHIFT] = 0x80;
    if(altGr)
        state[VK_CONTROL] = state[VK_MENU] = 0x80;
    if(g_capsLock)
        state[VK_CAPITAL] = 0x01;
    // A dead key types nothing by itself (its character comes with the next key).
    QString text;
    if(layoutText(k, state, &text) > 0)
        key.text = text;
    const BYTE none[256] = {};
    layoutText(k, none, &key.base);

    const KeyLabel label = keyLabel(key, MetaName::Win);
    if(label.label.isEmpty())
        return;
    InputMonitor* monitor = g_monitor;
    QMetaObject::invokeMethod(
        monitor, [monitor, label] { emit monitor->keyPressed(label.label, label.typed); }, Qt::QueuedConnection);
}

LRESULT CALLBACK keyboardProc(int code, WPARAM message, LPARAM data)
{
    if(code == HC_ACTION)
    {
        const auto& k = *reinterpret_cast<const KBDLLHOOKSTRUCT*>(data);
        if(message == WM_KEYDOWN || message == WM_SYSKEYDOWN)
            onKeyDown(k);
        else if(message == WM_KEYUP || message == WM_SYSKEYUP)
            g_held.erase(k.vkCode);
    }
    return CallNextHookEx(nullptr, code, message, data);
}

LRESULT CALLBACK mouseProc(int code, WPARAM message, LPARAM data)
{
    if(code == HC_ACTION && (message == WM_LBUTTONDOWN || message == WM_RBUTTONDOWN || message == WM_MBUTTONDOWN ||
                             message == WM_XBUTTONDOWN))
    {
        // Physical pixels: Qt makes the process per-monitor DPI aware.
        const POINT pt = reinterpret_cast<const MSLLHOOKSTRUCT*>(data)->pt;
        const QPoint physical(pt.x, pt.y);
        InputMonitor* monitor = g_monitor;
        QMetaObject::invokeMethod(
            monitor,
            [monitor, physical] {
                const QList<ScreenInfo> screens = platform::screens();
                const int si = screenAtPhysical(screens, physical);
                if(si >= 0)
                    emit monitor->mousePressed(toLogical(QRect(physical, QSize(1, 1)), screens[si]).topLeft());
            },
            Qt::QueuedConnection);
    }
    return CallNextHookEx(nullptr, code, message, data);
}
} // namespace

// The hooks live on a thread of their own with its own message loop: Windows drops low-level
// hooks that answer too slowly, which the GUI thread can't promise while it encodes.
struct InputMonitor::Private
{
    std::thread thread;
    DWORD threadId = 0;
};

InputMonitor::InputMonitor(QObject* parent)
    : QObject(parent)
    , d(std::make_unique<Private>())
{
}

InputMonitor::~InputMonitor()
{
    stop();
}

bool InputMonitor::supported()
{
    return true;
}

bool InputMonitor::start(QString* error)
{
    if(d->thread.joinable())
        return true;
    g_monitor = this;
    g_held.clear();
    g_capsLock = GetKeyState(VK_CAPITAL) & 1;

    std::promise<bool> hooked;
    std::future<bool> result = hooked.get_future();
    d->thread = std::thread([this, &hooked] {
        MSG msg;
        PeekMessageW(&msg, nullptr, WM_USER, WM_USER, PM_NOREMOVE); // the queue WM_QUIT is posted to
        d->threadId = GetCurrentThreadId();
        const HINSTANCE module = GetModuleHandleW(nullptr);
        HHOOK keyboard = SetWindowsHookExW(WH_KEYBOARD_LL, keyboardProc, module, 0);
        HHOOK mouse = SetWindowsHookExW(WH_MOUSE_LL, mouseProc, module, 0);
        hooked.set_value(keyboard && mouse);
        if(keyboard && mouse)
            while(GetMessageW(&msg, nullptr, 0, 0) > 0)
            {
            }
        if(keyboard)
            UnhookWindowsHookEx(keyboard);
        if(mouse)
            UnhookWindowsHookEx(mouse);
    });
    if(!result.get())
    {
        d->thread.join();
        g_monitor = nullptr;
        *error = "Could not listen to the keyboard and mouse.";
        return false;
    }
    return true;
}

void InputMonitor::stop()
{
    if(!d->thread.joinable())
        return;
    PostThreadMessageW(d->threadId, WM_QUIT, 0, 0);
    d->thread.join();
    g_monitor = nullptr;
}

} // namespace qr
