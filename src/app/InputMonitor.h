#pragma once

#include <QObject>
#include <QPoint>

namespace qr
{

// System-wide key and mouse-button presses while recording (opt-in): a listen-only event tap
// on macOS (InputMonitorMac.mm), which needs the Input Monitoring permission. Not available
// on X11 yet (InputMonitorX11.cpp).
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
    // `label` is what to show: the typed text when `typed`, else e.g. "⌘⇧R" or "↩".
    void keyPressed(QString label, bool typed);
    void mousePressed(QPoint globalLogical);

public:
    // Target of the macOS event tap's C callback (CG types erased to keep this header C++).
    static void* handleEventTrampoline(void* proxy, int type, void* event, void* self);

private:
    void* handleEvent(void* proxy, int type, void* event);

    void* m_tap = nullptr;    // CFMachPortRef
    void* m_source = nullptr; // CFRunLoopSourceRef
};

} // namespace qr
