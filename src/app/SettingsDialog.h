#pragma once

#include <QDialog>

#include <functional>

class QCheckBox;
class QComboBox;
class QKeySequenceEdit;
class QLabel;
class QLineEdit;
class QPushButton;
class QDoubleSpinBox;
class QSlider;
class QSpinBox;

namespace qr
{

struct Settings;
class Updater;

class SettingsDialog : public QDialog
{
    Q_OBJECT

public:
    explicit SettingsDialog(const Settings& current, QWidget* parent = nullptr);

    // Writes the dialog values into `s` (call after accept()).
    void apply(Settings& s) const;
    bool autostart() const;

    // Asked before installing an update, which restarts the app: true refuses ("stop the
    // recording first").
    void setBusyCheck(std::function<bool()> busy) { m_busy = std::move(busy); }

signals:
    // An update is installed and the app should quit now; it comes back by itself.
    void restartRequested();

private:
    void onUpdateButton();

    QComboBox* m_theme;
    QComboBox* m_textScale;
    QSlider* m_tileWidth;
    QKeySequenceEdit* m_hotkey;
    QKeySequenceEdit* m_shotHotkey;
    QKeySequenceEdit* m_laserHotkey;
    QDoubleSpinBox* m_laserFade;
    QSpinBox* m_fps;
    QCheckBox* m_mouse;
    QCheckBox* m_mic;
    QCheckBox* m_countdown;
    QCheckBox* m_input;
    QLineEdit* m_exportDir;
    QLineEdit* m_shotDir;
    QSpinBox* m_shotDelay;
    QCheckBox* m_autostart;
    Updater* m_updater;
    QPushButton* m_updateButton;
    QLabel* m_updateStatus;
    enum class UpdateAction
    {
        Check,
        Install,
        OpenPage,
    } m_updateAction = UpdateAction::Check;
    std::function<bool()> m_busy;
};

} // namespace qr
