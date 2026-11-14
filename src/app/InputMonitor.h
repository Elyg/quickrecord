#pragma once

#include <QObject>
#include <QPoint>

#include <memory>

namespace qr
{

// System-wide key and mouse-button presses, listened to only while recording (opt-in):
//   macOS   a listen-only event tap (InputMonitorMac.mm), which needs the Input Monitoring
//           permission;
//   X11     XInput2 raw events on a connection of its own (InputMonitorX11.cpp);
//   Windows low-level keyboard and mouse hooks on a thread of their own (InputMonitorWin.cpp).
class InputMonitor : public QObject
{
    Q_OBJECT

public:
    explicit InputMonitor(QObject* parent = nullptr);
    ~InputMonitor() override;

    static bool supported();
    // False (with a reason, e.g. how to grant the permission) when it can't listen.
    bool start(QString* error);
    void stop();

signals:
    // `label` is what to show: the typed text when `typed`, else e.g. "⌘⇧R", "Ctrl+Shift+R" or "↩".
    void keyPressed(QString label, bool typed);
    void mousePressed(QPoint globalLogical);

#ifdef Q_OS_MACOS
public:
    // Target of the macOS event tap's C callback (CG types erased to keep this header C++).
    static void* handleEventTrampoline(void* proxy, int type, void* event, void* self);

private:
    void* handleEvent(void* proxy, int type, void* event);

    void* m_tap = nullptr;    // CFMachPortRef
    void* m_source = nullptr; // CFRunLoopSourceRef
#else
private:
    struct Private;
    std::unique_ptr<Private> d;
#endif
};

} // namespace qr
