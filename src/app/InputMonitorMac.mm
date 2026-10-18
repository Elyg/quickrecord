#include "app/InputMonitor.h"

#include <QString>

#import <ApplicationServices/ApplicationServices.h>
#import <Carbon/Carbon.h>

namespace qr
{

namespace
{
// Keys shown as a symbol (kVK_* codes).
QString keySymbol(int64_t code)
{
    switch(code)
    {
        case kVK_Return:
        case kVK_ANSI_KeypadEnter: return "↩";
        case kVK_Tab: return "⇥";
        case kVK_Space: return "Space";
        case kVK_Delete: return "⌫";
        case kVK_ForwardDelete: return "⌦";
        case kVK_Escape: return "⎋";
        case kVK_LeftArrow: return "←";
        case kVK_RightArrow: return "→";
        case kVK_DownArrow: return "↓";
        case kVK_UpArrow: return "↑";
        case kVK_Home: return "↖";
        case kVK_End: return "↘";
        case kVK_PageUp: return "⇞";
        case kVK_PageDown: return "⇟";
        default: break;
    }
    static constexpr int64_t kFKeys[] = {kVK_F1,  kVK_F2,  kVK_F3,  kVK_F4,  kVK_F5,  kVK_F6,  kVK_F7,
                                         kVK_F8,  kVK_F9,  kVK_F10, kVK_F11, kVK_F12, kVK_F13, kVK_F14,
                                         kVK_F15, kVK_F16, kVK_F17, kVK_F18, kVK_F19, kVK_F20};
    for(int i = 0; i < int(std::size(kFKeys)); ++i)
        if(kFKeys[i] == code)
            return QStringLiteral("F%1").arg(i + 1);
    return {};
}

// The key's character in the current layout without modifiers ("r" for ⌘⇧R).
QString baseCharacter(int64_t code)
{
    QString out;
    TISInputSourceRef source = TISCopyCurrentKeyboardLayoutInputSource();
    if(!source)
        return out;
    if(auto data = static_cast<CFDataRef>(TISGetInputSourceProperty(source, kTISPropertyUnicodeKeyLayoutData)))
    {
        const auto* layout = reinterpret_cast<const UCKeyboardLayout*>(CFDataGetBytePtr(data));
        UInt32 deadKeys = 0;
        UniChar chars[4];
        UniCharCount length = 0;
        if(UCKeyTranslate(layout, UInt16(code), kUCKeyActionDisplay, 0, LMGetKbdType(), kUCKeyTranslateNoDeadKeysMask,
                          &deadKeys, 4, &length, chars) == noErr)
            out = QString::fromUtf16(reinterpret_cast<const char16_t*>(chars), qsizetype(length));
    }
    CFRelease(source);
    return out;
}

CGEventRef onTapEvent(CGEventTapProxy proxy, CGEventType type, CGEventRef event, void* self)
{
    return static_cast<CGEventRef>(InputMonitor::handleEventTrampoline(proxy, int(type), event, self));
}
} // namespace

InputMonitor::InputMonitor(QObject* parent)
    : QObject(parent)
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
    if(m_tap)
        return true;
    if(!CGPreflightListenEventAccess())
    {
        CGRequestListenEventAccess(); // shows the prompt / adds us to the list, unticked
        *error = "Allow QuickRecord under System Settings › Privacy & Security › Input Monitoring, then restart it.";
        return false;
    }
    const CGEventMask mask = CGEventMaskBit(kCGEventKeyDown) | CGEventMaskBit(kCGEventLeftMouseDown) |
                             CGEventMaskBit(kCGEventRightMouseDown) | CGEventMaskBit(kCGEventOtherMouseDown);
    CFMachPortRef tap = CGEventTapCreate(kCGSessionEventTap, kCGHeadInsertEventTap, kCGEventTapOptionListenOnly, mask,
                                         &onTapEvent, this);
    if(!tap)
    {
        *error = "Could not listen to the keyboard and mouse.";
        return false;
    }
    CFRunLoopSourceRef source = CFMachPortCreateRunLoopSource(kCFAllocatorDefault, tap, 0);
    CFRunLoopAddSource(CFRunLoopGetMain(), source, kCFRunLoopCommonModes);
    CGEventTapEnable(tap, true);
    m_tap = tap;
    m_source = source;
    return true;
}

void InputMonitor::stop()
{
    if(!m_tap)
        return;
    auto tap = static_cast<CFMachPortRef>(m_tap);
    auto source = static_cast<CFRunLoopSourceRef>(m_source);
    CGEventTapEnable(tap, false);
    CFRunLoopRemoveSource(CFRunLoopGetMain(), source, kCFRunLoopCommonModes);
    CFMachPortInvalidate(tap);
    CFRelease(source);
    CFRelease(tap);
    m_tap = nullptr;
    m_source = nullptr;
}

void* InputMonitor::handleEventTrampoline(void* proxy, int type, void* event, void* self)
{
    return static_cast<InputMonitor*>(self)->handleEvent(proxy, type, event);
}

void* InputMonitor::handleEvent(void*, int type, void* nativeEvent)
{
    auto event = static_cast<CGEventRef>(nativeEvent);
    switch(CGEventType(type))
    {
        case kCGEventTapDisabledByTimeout:
        case kCGEventTapDisabledByUserInput:
            // The system switches slow taps off; ours only reads, so just turn it back on.
            if(m_tap)
                CGEventTapEnable(static_cast<CFMachPortRef>(m_tap), true);
            break;
        case kCGEventLeftMouseDown:
        case kCGEventRightMouseDown:
        case kCGEventOtherMouseDown:
        {
            const CGPoint p = CGEventGetLocation(event); // global points, like Qt's coordinates
            emit mousePressed(QPoint(qRound(p.x), qRound(p.y)));
            break;
        }
        case kCGEventKeyDown:
        {
            if(CGEventGetIntegerValueField(event, kCGKeyboardEventAutorepeat))
                break;
            const CGEventFlags flags = CGEventGetFlags(event);
            const bool cmd = flags & kCGEventFlagMaskCommand;
            const bool ctrl = flags & kCGEventFlagMaskControl;
            const int64_t code = CGEventGetIntegerValueField(event, kCGKeyboardEventKeycode);
            const QString symbol = keySymbol(code);

            UniChar chars[8];
            UniCharCount length = 0;
            CGEventKeyboardGetUnicodeString(event, 8, &length, chars);
            const QString text = QString::fromUtf16(reinterpret_cast<const char16_t*>(chars), qsizetype(length));

            // Typing: text without ⌘/⌃ (⇧ and ⌥ only choose the character).
            if(!cmd && !ctrl && (code == kVK_Space || (symbol.isEmpty() && !text.isEmpty() && text.at(0).isPrint())))
            {
                emit keyPressed(code == kVK_Space ? QStringLiteral(" ") : text, true);
                break;
            }
            QString key = symbol.isEmpty() ? baseCharacter(code).toUpper() : symbol;
            if(key.isEmpty())
                break;
            QString mods;
            if(ctrl)
                mods += "⌃";
            if(flags & kCGEventFlagMaskAlternate)
                mods += "⌥";
            if(flags & kCGEventFlagMaskShift)
                mods += "⇧";
            if(cmd)
                mods += "⌘";
            emit keyPressed(mods + key, false);
            break;
        }
        default: break;
    }
    return nativeEvent;
}

} // namespace qr
