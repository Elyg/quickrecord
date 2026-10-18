#pragma once

#include <QDialog>

class QCheckBox;
class QComboBox;
class QKeySequenceEdit;
class QLineEdit;
class QSpinBox;

namespace qr
{

struct Settings;

class SettingsDialog : public QDialog
{
    Q_OBJECT

public:
    explicit SettingsDialog(const Settings& current, QWidget* parent = nullptr);

    // Writes the dialog values into `s` (call after accept()).
    void apply(Settings& s) const;
    bool autostart() const;

private:
    QComboBox* m_theme;
    QComboBox* m_textScale;
    QKeySequenceEdit* m_hotkey;
    QKeySequenceEdit* m_shotHotkey;
    QSpinBox* m_fps;
    QCheckBox* m_mouse;
    QCheckBox* m_mic;
    QCheckBox* m_countdown;
    QCheckBox* m_input;
    QLineEdit* m_exportDir;
    QLineEdit* m_shotDir;
    QCheckBox* m_autostart;
};

} // namespace qr
