#pragma once

#include <QAbstractNativeEventFilter>
#include <QElapsedTimer>
#include <QKeySequence>
#include <QObject>
#include <cstdint>

namespace qr
{

// System-wide shortcut: XGrabKey on the root window (GlobalHotkey.cpp), or a Carbon
// RegisterEventHotKey on macOS (GlobalHotkeyMac.cpp).
class GlobalHotkey : public QObject, public QAbstractNativeEventFilter
{
    Q_OBJECT

public:
    explicit GlobalHotkey(QObject* parent = nullptr);
    ~GlobalHotkey() override;

    // Grabs the shortcut, replacing any previous one. False (and nothing grabbed) when the
    // key can't be mapped or another client already owns the combination.
    bool setShortcut(const QKeySequence& seq, QString* error);
    void clear();

    bool nativeEventFilter(const QByteArray& eventType, void* message, qintptr* result) override;

signals:
    void activated();

private:
#ifdef Q_OS_MACOS
    void* m_ref = nullptr; // EventHotKeyRef
    uint32_t m_id = 0;
#else
    uint8_t m_keycode = 0;
    uint16_t m_modifiers = 0;
    QElapsedTimer m_lastPress;
#endif
};

} // namespace qr
