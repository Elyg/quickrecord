#include "app/SettingsDialog.h"

#include "app/Icons.h"
#include "app/InputMonitor.h"
#include "app/Settings.h"
#include "app/Updater.h"

#include "BuildInfo.h"

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
#include <QSlider>
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

    m_tileWidth = new QSlider(Qt::Horizontal, this);
    m_tileWidth->setRange(kMinHistoryTileWidth, kMaxHistoryTileWidth);
    m_tileWidth->setSingleStep(kHistoryTileStep);
    m_tileWidth->setPageStep(kHistoryTileStep * 3);
    m_tileWidth->setValue(current.historyTileWidth);
    m_tileWidth->setToolTip("Width of the thumbnails in the History; Ctrl+wheel over them changes it too");
    auto* tileWidthValue = new QLabel(this);
    tileWidthValue->setMinimumWidth(48);
    const auto showTileWidth = [tileWidthValue](int width) { tileWidthValue->setText(QStringLiteral("%1 px").arg(width)); };
    showTileWidth(m_tileWidth->value());
    connect(m_tileWidth, &QSlider::valueChanged, this, showTileWidth);
    auto* tileWidthRow = new QHBoxLayout;
    tileWidthRow->addWidget(m_tileWidth, 1);
    tileWidthRow->addWidget(tileWidthValue);

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

    m_mouse = new QCheckBox("Show the mouse cursor in recordings", this);
    m_mouse->setChecked(current.drawMouse);
    m_mic = new QCheckBox("Record the microphone (MP4 only)", this);
    m_mic->setChecked(current.recordMic);
    m_countdown = new QCheckBox("3-second countdown before recording", this);
    m_countdown->setChecked(current.countdown);
    m_input = new QCheckBox("Capture keystrokes and mouse clicks while recording", this);
    m_input->setChecked(current.recordInput && InputMonitor::supported());
    m_input->setEnabled(InputMonitor::supported());
    m_input->setToolTip(InputMonitor::supported()
                            ? "Only while a recording runs: saved next to it, and the editor can show them on "
                              "the video (keys in a corner, a ring where you click). Everything you type during "
                              "a recording is included."
                            : "Not available on this system.");

    m_exportDir = new QLineEdit(current.exportDir, this);
    m_shotDir = new QLineEdit(current.screenshotDir, this);
    m_shotDelay = new QSpinBox(this);
    m_shotDelay->setRange(0, kMaxScreenshotDelay);
    m_shotDelay->setSpecialValueText("Off");
    m_shotDelay->setSuffix(" s");
    m_shotDelay->setValue(current.screenshotDelay);
    m_shotDelay->setToolTip("Wait this long after picking the area before taking the screenshot,\n"
                            "e.g. to open a menu first. T in the area picker changes it too.");

    m_autostart = new QCheckBox("Start QuickRecord when I log in", this);
    m_autostart->setChecked(Settings::autostartEnabled());

    auto* form = new QFormLayout;
    form->setVerticalSpacing(10);
    form->setHorizontalSpacing(16);
    form->setLabelAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    form->addRow("Appearance", m_theme);
    form->addRow("Text size", m_textScale);
    form->addRow("Thumbnail size", tileWidthRow);
    form->addRow("Record hotkey", m_hotkey);
    form->addRow("Screenshot hotkey", m_shotHotkey);
    form->addRow("Laser pointer hotkey", m_laserHotkey);
    form->addRow("Laser pointer fades after", m_laserFade);
    form->addRow("Screenshot timer", m_shotDelay);
    form->addRow("Frame rate", m_fps);
    form->addRow("", m_mouse);
    form->addRow("", m_mic);
    form->addRow("", m_countdown);
    form->addRow("", m_input);
    form->addRow("Recordings folder", folderRow(m_exportDir, "Recordings folder", this));
    form->addRow("Screenshots folder", folderRow(m_shotDir, "Screenshots folder", this));
    form->addRow("", m_autostart);

    m_updater = new Updater(QUICKRECORD_VERSION, this);
    m_updateButton = new QPushButton("Check for updates", this);
    m_updateStatus = new QLabel(this);
    m_updateStatus->setStyleSheet("color: palette(placeholder-text);");
    m_updateStatus->setWordWrap(true);
    m_updateStatus->hide();
    auto* versionRow = new QHBoxLayout;
    versionRow->addWidget(new QLabel("QuickRecord " QUICKRECORD_VERSION, this));
    versionRow->addStretch(1);
    versionRow->addWidget(m_updateButton);
    form->addRow("Version", versionRow);
    form->addRow("", m_updateStatus);
    connect(m_updateButton, &QPushButton::clicked, this, &SettingsDialog::onUpdateButton);
    const auto status = [this](const QString& text) {
        m_updateStatus->setText(text);
        m_updateStatus->setVisible(!text.isEmpty());
    };
    connect(m_updater, &Updater::checked, this, [this, status](const Updater::Result& r) {
        m_updateButton->setEnabled(true);
        switch(r.status)
        {
            case Updater::Status::UpToDate:
                status("You have the latest version.");
                m_updateButton->setText("Check for updates");
                m_updateAction = UpdateAction::Check;
                break;
            case Updater::Status::DevBuild:
                status("Development build · the latest release is " + r.latest + ".");
                m_updateButton->setText("Open release page");
                m_updateAction = UpdateAction::OpenPage;
                break;
            case Updater::Status::Available:
                status("QuickRecord " + r.latest + " is available." +
                       (r.canInstall ? QString(" It restarts once installed.") : QString()));
                m_updateButton->setText((r.canInstall ? "Update to " : "Download ") + r.latest);
                m_updateAction = r.canInstall ? UpdateAction::Install : UpdateAction::OpenPage;
                break;
            case Updater::Status::Failed:
                status(r.message);
                m_updateButton->setText("Check for updates");
                m_updateAction = UpdateAction::Check;
                break;
        }
    });
    connect(m_updater, &Updater::progress, this,
            [status](int percent) { status(QString("Downloading… %1 %").arg(percent)); });
    connect(m_updater, &Updater::failed, this, [this, status](const QString& message) {
        status(message);
        m_updateButton->setEnabled(true);
        m_updateButton->setText("Check for updates");
        m_updateAction = UpdateAction::Check;
    });
    connect(m_updater, &Updater::installed, this, [this, status] {
        status("Installed; restarting…");
        if(m_updater->finish())
            emit restartRequested();
    });

    auto* note = new QLabel("Each recording or screenshot sits in its folder next to its edited version "
                            "(\"…-edited.gif\" / \".mp4\" / \".png\").\n"
                            "In the area picker, Tab switches between recording and screenshot, "
                            "T sets the screenshot timer.",
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
    s.historyTileWidth = m_tileWidth->value();
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
    s.screenshotDelay = m_shotDelay->value();
}

bool SettingsDialog::autostart() const
{
    return m_autostart->isChecked();
}

void SettingsDialog::onUpdateButton()
{
    switch(m_updateAction)
    {
        case UpdateAction::Check:
            m_updateButton->setEnabled(false);
            m_updateStatus->setText("Checking…");
            m_updateStatus->show();
            m_updater->check();
            break;
        case UpdateAction::Install:
            if(m_busy && m_busy())
            {
                m_updateStatus->setText("Stop the recording first: updating restarts QuickRecord.");
                return;
            }
            m_updateButton->setEnabled(false);
            m_updateStatus->setText("Downloading…");
            m_updater->install();
            break;
        case UpdateAction::OpenPage: m_updater->openReleasePage(); break;
    }
}

} // namespace qr
