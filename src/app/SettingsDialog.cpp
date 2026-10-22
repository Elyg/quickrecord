#include "app/SettingsDialog.h"

#include "app/Icons.h"
#include "app/InputMonitor.h"
#include "app/Settings.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QDoubleSpinBox>
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

    m_theme = new QComboBox(this);
    m_theme->addItem("Follow the system", "system");
    m_theme->addItem("Dark", "dark");
    m_theme->addItem("Light", "light");
    m_theme->setCurrentIndex(std::max(0, m_theme->findData(current.theme)));

    m_textScale = new QComboBox(this);
    for(int percent : {80, 90, 100, 110, 125, 150})
        m_textScale->addItem(percent == 100 ? QStringLiteral("100% (default)") : QStringLiteral("%1%").arg(percent),
                             percent);
    if(m_textScale->findData(current.textScale) < 0) // set by hand in the config file
        m_textScale->addItem(QStringLiteral("%1%").arg(current.textScale), current.textScale);
    m_textScale->setCurrentIndex(m_textScale->findData(current.textScale));
    m_textScale->setToolTip("Size of the text in QuickRecord's windows; smaller makes the editors fit narrower windows");

    m_hotkey = hotkeyEdit(current.hotkey, this);
    m_shotHotkey = hotkeyEdit(current.screenshotHotkey, this);
    m_laserHotkey = hotkeyEdit(current.laserHotkey, this);
    m_laserHotkey->setToolTip("Turns the laser pointer on and off: drag to draw a stroke that fades (Esc or "
                              "right-click also turn it off). Over the recorded area while recording, else the screen.");
    m_laserFade = new QDoubleSpinBox(this);
    m_laserFade->setRange(0.2, 5.0);
    m_laserFade->setSingleStep(0.1);
    m_laserFade->setDecimals(1);
    m_laserFade->setSuffix(" s");
    m_laserFade->setValue(current.laserFadeMs / 1000.0);
    m_laserFade->setToolTip("How long a laser stroke stays after you let go of the mouse, fade included");

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
    m_input = new QCheckBox("Record keystrokes and mouse clicks", this);
    m_input->setChecked(current.recordInput && InputMonitor::supported());
    m_input->setEnabled(InputMonitor::supported());
    m_input->setToolTip(InputMonitor::supported()
                            ? "Saved next to the recording; the editor can show them on the video (keys in a "
                              "corner, a ring where you click). Everything you type while recording is logged."
                            : "Not available on X11 yet.");

    m_exportDir = new QLineEdit(current.exportDir, this);
    m_shotDir = new QLineEdit(current.screenshotDir, this);

    m_autostart = new QCheckBox("Start QuickRecord when I log in", this);
    m_autostart->setChecked(Settings::autostartEnabled());

    auto* form = new QFormLayout;
    form->setVerticalSpacing(10);
    form->setHorizontalSpacing(16);
    form->setLabelAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    form->addRow("Appearance", m_theme);
    form->addRow("Text size", m_textScale);
    form->addRow("Record hotkey", m_hotkey);
    form->addRow("Screenshot hotkey", m_shotHotkey);
    form->addRow("Laser pointer hotkey", m_laserHotkey);
    form->addRow("Laser pointer fades after", m_laserFade);
    form->addRow("Frame rate", m_fps);
    form->addRow("", m_mouse);
    form->addRow("", m_mic);
    form->addRow("", m_countdown);
    form->addRow("", m_input);
    form->addRow("Recordings folder", folderRow(m_exportDir, "Recordings folder", this));
    form->addRow("Screenshots folder", folderRow(m_shotDir, "Screenshots folder", this));
    form->addRow("", m_autostart);

    auto* note = new QLabel("Each recording or screenshot sits in its folder next to its edited version "
                            "(\"…-edited.gif\" / \".mp4\" / \".png\").\n"
                            "In the area picker, Tab switches between recording and screenshot.",
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
    s.theme = m_theme->currentData().toString();
    s.textScale = m_textScale->currentData().toInt();
    s.hotkey = m_hotkey->keySequence().toString(QKeySequence::PortableText);
    s.screenshotHotkey = m_shotHotkey->keySequence().toString(QKeySequence::PortableText);
    s.laserHotkey = m_laserHotkey->keySequence().toString(QKeySequence::PortableText);
    s.laserFadeMs = int(m_laserFade->value() * 1000 + 0.5);
    s.fps = m_fps->value();
    s.drawMouse = m_mouse->isChecked();
    s.recordMic = m_mic->isChecked();
    s.countdown = m_countdown->isChecked();
    s.recordInput = m_input->isChecked();
    s.exportDir = m_exportDir->text().trimmed();
    s.screenshotDir = m_shotDir->text().trimmed();
}

bool SettingsDialog::autostart() const
{
    return m_autostart->isChecked();
}

} // namespace qr
