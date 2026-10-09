#include "app/SettingsDialog.h"

#include "app/Icons.h"
#include "app/Settings.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QToolButton>
#include <QVBoxLayout>

namespace qr
{

namespace
{
QKeySequenceEdit* hotkeyEdit(const QString& value, QWidget* parent)
{
    auto* edit = new QKeySequenceEdit(QKeySequence(value), parent);
    edit->setMaximumSequenceLength(1);
    edit->setClearButtonEnabled(true);
    return edit;
}

// Line edit plus a "…" button that picks a folder.
QHBoxLayout* folderRow(QLineEdit* edit, const QString& title, QWidget* parent)
{
    auto* browse = new QToolButton(parent);
    browse->setText("…");
    auto* row = new QHBoxLayout;
    row->addWidget(edit, 1);
    row->addWidget(browse);
    QObject::connect(browse, &QToolButton::clicked, parent, [edit, title, parent] {
        const QString dir = QFileDialog::getExistingDirectory(parent, title, edit->text());
        if(!dir.isEmpty())
            edit->setText(dir);
    });
    return row;
}
} // namespace

SettingsDialog::SettingsDialog(const Settings& current, QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle("QuickRecord — Settings");
    setWindowIcon(icons::app());

    m_hotkey = hotkeyEdit(current.hotkey, this);
    m_shotHotkey = hotkeyEdit(current.screenshotHotkey, this);

    m_fps = new QSpinBox(this);
    m_fps->setRange(5, 60);
    m_fps->setValue(current.fps);
    m_fps->setSuffix(" fps");

    m_mouse = new QCheckBox("Show the mouse cursor", this);
    m_mouse->setChecked(current.drawMouse);
    m_mic = new QCheckBox("Record the microphone (MP4 only)", this);
    m_mic->setChecked(current.recordMic);
    m_countdown = new QCheckBox("3-second countdown before recording", this);
    m_countdown->setChecked(current.countdown);

    m_exportDir = new QLineEdit(current.exportDir, this);
    m_shotDir = new QLineEdit(current.screenshotDir, this);

    m_autostart = new QCheckBox("Start QuickRecord when I log in", this);
    m_autostart->setChecked(Settings::autostartEnabled());

    auto* form = new QFormLayout;
    form->addRow("Record hotkey", m_hotkey);
    form->addRow("Screenshot hotkey", m_shotHotkey);
    form->addRow("Frame rate", m_fps);
    form->addRow("", m_mouse);
    form->addRow("", m_mic);
    form->addRow("", m_countdown);
    form->addRow("Export folder", folderRow(m_exportDir, "Export folder", this));
    form->addRow("Screenshot folder", folderRow(m_shotDir, "Screenshot folder", this));
    form->addRow("", m_autostart);

    auto* note = new QLabel(QStringLiteral("In the area picker, Tab switches between recording and screenshot.\n"
                                           "Recordings are kept in %1, original screenshots in %2")
                                .arg(Settings::mastersDir(), Settings::shotsDir()),
                            this);
    note->setStyleSheet("color: palette(placeholder-text);");
    note->setWordWrap(true);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto* root = new QVBoxLayout(this);
    root->addLayout(form);
    root->addWidget(note);
    root->addStretch(1);
    root->addWidget(buttons);
    setMinimumWidth(460);
}

void SettingsDialog::apply(Settings& s) const
{
    s.hotkey = m_hotkey->keySequence().toString(QKeySequence::PortableText);
    s.screenshotHotkey = m_shotHotkey->keySequence().toString(QKeySequence::PortableText);
    s.fps = m_fps->value();
    s.drawMouse = m_mouse->isChecked();
    s.recordMic = m_mic->isChecked();
    s.countdown = m_countdown->isChecked();
    s.exportDir = m_exportDir->text().trimmed();
    s.screenshotDir = m_shotDir->text().trimmed();
}

bool SettingsDialog::autostart() const
{
    return m_autostart->isChecked();
}

} // namespace qr
