#include "app/Platform.h"

#include <QColorSpace>
#include <QGuiApplication>
#include <QPixmap>
#include <QScreen>
#include <QTimer>
#include <QWidget>
#include <QWindow>
#include <algorithm>
#include <unistd.h>

#import <AppKit/AppKit.h>
#import <CoreGraphics/CoreGraphics.h>
#import <CoreServices/CoreServices.h>

// Target for the "reopen application" Apple event.
@interface QRReopenHandler : NSObject
{
@public
    std::function<void()> callback;
}
- (void)handleReopen:(NSAppleEventDescriptor*)event withReplyEvent:(NSAppleEventDescriptor*)reply;
@end

@implementation QRReopenHandler
- (void)handleReopen:(NSAppleEventDescriptor*)event withReplyEvent:(NSAppleEventDescriptor*)reply
{
    if(callback)
        callback();
}
@end

namespace qr::platform
{

namespace
{
int screenIndexAt(const QList<ScreenInfo>& all, const QRect& physical)
{
    return std::max(0, screenAtPhysical(all, physical.center()));
}

// ffmpeg's avfoundation numbers screens in CGGetActiveDisplayList order ("Capture screen N").
// Qt and CoreGraphics both use global points with the origin at the main display's top-left.
int avfoundationScreenIndex(const QRect& logicalGeometry)
{
    CGDirectDisplayID ids[16];
    uint32_t count = 0;
    if(CGGetActiveDisplayList(16, ids, &count) != kCGErrorSuccess)
        return 0;
    for(uint32_t i = 0; i < count; ++i)
    {
        const CGRect b = CGDisplayBounds(ids[i]);
        if(QRect(int(b.origin.x), int(b.origin.y), int(b.size.width), int(b.size.height)) == logicalGeometry)
            return int(i);
    }
    return 0;
}
} // namespace

QString unsupportedReason()
{
    return {};
}

void init()
{
    // Shows the Screen Recording prompt once, instead of on the first (empty) capture.
    if(!CGPreflightScreenCaptureAccess())
        CGRequestScreenCaptureAccess();
}

void onReopen(std::function<void()> callback)
{
    static QRReopenHandler* handler = [[QRReopenHandler alloc] init];
    handler->callback = std::move(callback);
    static const auto install = [] {
        [[NSAppleEventManager sharedAppleEventManager] setEventHandler:handler
                                                           andSelector:@selector(handleReopen:withReplyEvent:)
                                                         forEventClass:kCoreEventClass
                                                            andEventID:kAEReopenApplication];
    };
    // NSApplication installs its own handler while it finishes launching (once the event
    // loop runs), so install ours after that, whichever of these comes last.
    [[NSNotificationCenter defaultCenter] addObserverForName:NSApplicationDidFinishLaunchingNotification
                                                      object:nil
                                                       queue:nil
                                                  usingBlock:^(NSNotification*) { install(); }];
    QTimer::singleShot(0, [] { install(); });
}

QList<ScreenInfo> screens()
{
    QList<ScreenInfo> out;
    qreal maxDpr = 1.0;
    for(QScreen* s : QGuiApplication::screens())
        maxDpr = std::max(maxDpr, s->devicePixelRatio());
    for(QScreen* s : QGuiApplication::screens())
        out << ScreenInfo{s->geometry(), s->devicePixelRatio(), maxDpr};
    return out;
}

QRect captureBounds(const QRect& physical)
{
    const QList<ScreenInfo> all = screens();
    return all.isEmpty() ? QRect() : physicalGeometry(all[screenIndexAt(all, physical)]);
}

QImage grab(const QRect& physical)
{
    const QList<ScreenInfo> all = screens();
    const QList<QScreen*> qscreens = QGuiApplication::screens();
    if(all.isEmpty() || all.size() != qscreens.size())
        return {};
    const int si = screenIndexAt(all, physical);
    const QRect logical = toLogical(physical, all[si]).translated(-all[si].geometry.topLeft());
    QImage image = qscreens[si]->grabWindow(0, logical.x(), logical.y(), logical.width(), logical.height()).toImage();
    if(image.isNull())
        return {};
    image.setDevicePixelRatio(1.0);
    // The grab is tagged with the display's ICC profile, whose bytes Qt doesn't keep alive:
    // QImage::save crashes embedding it. Drop the tag (pixels stay as they are), which also
    // matches the untagged images X11 gives us.
    image.setColorSpace(QColorSpace());
    if(image.size() != physical.size()) // logical rounding at the edges
        image = image.scaled(physical.size(), Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    return image.convertToFormat(QImage::Format_RGB32);
}

QString screenAccessHint()
{
    return "Allow QuickRecord under System Settings › Privacy & Security › Screen & System Audio Recording, "
           "then restart it.";
}

QList<QRect> visibleWindowRects()
{
    QList<QRect> out;
    const QList<ScreenInfo> all = screens();
    if(all.isEmpty())
        return out;
    CFArrayRef list = CGWindowListCopyWindowInfo(kCGWindowListOptionOnScreenOnly | kCGWindowListExcludeDesktopElements,
                                                 kCGNullWindowID);
    if(!list)
        return out;
    const pid_t self = getpid();
    // Front to back, like _NET_CLIENT_LIST_STACKING reversed.
    for(NSDictionary* info in (__bridge NSArray*)list)
    {
        // Layer 0 holds normal app windows; the menu bar, Dock and status items sit above.
        if([info[(__bridge NSString*)kCGWindowLayer] intValue] != 0)
            continue;
        if([info[(__bridge NSString*)kCGWindowAlpha] doubleValue] <= 0.0)
            continue;
        if([info[(__bridge NSString*)kCGWindowOwnerPID] intValue] == self)
            continue;
        CGRect b;
        if(!CGRectMakeWithDictionaryRepresentation((__bridge CFDictionaryRef)info[(__bridge NSString*)kCGWindowBounds],
                                                   &b))
            continue;
        const QRect logical(int(b.origin.x), int(b.origin.y), int(b.size.width), int(b.size.height));
        if(logical.width() <= 1 || logical.height() <= 1)
            continue;
        const int si = screenAtLogical(all, logical.center());
        out << toPhysical(logical, all[std::max(0, si)]);
    }
    CFRelease(list);
    return out;
}

void setCaptureSource(CaptureParams& params, const QRect& physical)
{
    const QList<ScreenInfo> all = screens();
    params.source = CaptureSource::AVFoundation;
    if(all.isEmpty())
    {
        params.display = "Capture screen 0";
        params.physicalRect = physical;
        return;
    }
    const ScreenInfo& screen = all[screenIndexAt(all, physical)];
    params.display = QStringLiteral("Capture screen %1").arg(avfoundationScreenIndex(screen.geometry));
    params.physicalRect = physical.translated(-physicalGeometry(screen).topLeft());
}

void keepOnTop(QWidget* widget, const QRect& geometry)
{
    NSView* view = reinterpret_cast<NSView*>(widget->winId());
    NSWindow* window = view.window;
    if(!window)
        return;
    // Qt::Tool windows otherwise drop to normal level (behind the window being recorded)
    // and hide whenever another app becomes active. Qt's own switch for that:
    widget->windowHandle()->setProperty("_q_macAlwaysShowToolWindow", true);
    window.level = NSStatusWindowLevel; // above the menu bar and the Dock
    window.collectionBehavior = NSWindowCollectionBehaviorCanJoinAllSpaces |
                                NSWindowCollectionBehaviorFullScreenAuxiliary |
                                NSWindowCollectionBehaviorStationary | NSWindowCollectionBehaviorIgnoresCycle;
    window.hidesOnDeactivate = NO;
    if(!geometry.isEmpty())
    {
        // Cocoa's origin is the bottom-left of the main (menu bar) display, y up.
        const CGFloat mainHeight = NSScreen.screens.firstObject.frame.size.height;
        [window setFrame:NSMakeRect(geometry.x(), mainHeight - geometry.y() - geometry.height(), geometry.width(),
                                    geometry.height())
                 display:YES];
    }
}

void activateApp()
{
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
    // The replacement (-activate, macOS 14) only asks; a hotkey press should take focus.
    [NSApp activateIgnoringOtherApps:YES];
#pragma clang diagnostic pop
}

} // namespace qr::platform
