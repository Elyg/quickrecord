#include "app/ScreenshotEditor.h"

#include "app/CaptionBar.h"
#include "app/Clipboard.h"
#include "app/Icons.h"
#include "app/Settings.h"
#include "app/VideoView.h"

#include <QCloseEvent>
#include <QDir>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QImageReader>
#include <QLabel>
#include <QLocale>
#include <QMessageBox>
#include <QPainter>
#include <QPushButton>
#include <QShortcut>
#include <QToolButton>
#include <QVBoxLayout>

namespace qr
{

ScreenshotEditor::ScreenshotEditor(const HistoryEntry& entry, Settings& settings)
    : m_entry(entry)
    , m_settings(settings)
{
    setAttribute(Qt::WA_DeleteOnClose);
    setWindowTitle(QStringLiteral("QuickRecord — %1").arg(entry.id));
    setWindowIcon(icons::app());

    QImageReader reader(entry.master);
    m_image = reader.read();

    m_view = new VideoView(this);
    m_view->setMinimumSize(320, 180);
    m_view->setCropAlignment(1);
    m_view->setToolTip("Wheel: zoom · drag: pan · double-click / 0 / 1: fit ↔ 100%");

    m_cropEdit = new QToolButton(this);
    m_cropEdit->setText("Crop");
    m_cropEdit->setCheckable(true);
    m_cropEdit->setToolTip("Drag the handles on the preview; drag outside the box to draw a new one");
    m_cropReset = new QToolButton(this);
    m_cropReset->setText("Reset");
    m_cropReset->setToolTip("Use the whole screenshot");
    m_cropLabel = new QLabel(this);
    m_caption = new CaptionBar(settings, "Font size in pixels of the screenshot", this);

    m_status = new QLabel(this);
    m_status->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_status->setWordWrap(true);

    m_discard = new QPushButton("Discard", this);
    m_discard->setToolTip("Delete this screenshot");
    m_cancel = new QPushButton("Cancel", this);
    m_cancel->setToolTip("Close without keeping this session's crop and text changes");
    m_copy = new QPushButton("Copy", this);
    m_copy->setToolTip("Copy the PNG to the clipboard without saving it (Ctrl+Shift+C)");
    m_save = new QPushButton("Save", this);
    m_save->setToolTip("Save the PNG to the screenshot folder (Ctrl+S)");
    m_saveCopy = new QPushButton("Save && Copy", this);
    m_saveCopy->setToolTip("Save the PNG to the screenshot folder and copy it to the clipboard (Ctrl+C)");
    m_saveCopy->setDefault(true);

    auto* root = new QVBoxLayout(this);
    root->addWidget(m_view, 1);

    auto* editRow = new QHBoxLayout;
    editRow->addWidget(m_cropEdit);
    editRow->addWidget(m_cropReset);
    editRow->addWidget(m_cropLabel);
    editRow->addSpacing(16);
    editRow->addWidget(m_caption, 1);
    root->addLayout(editRow);
    root->addWidget(m_status);

    auto* buttons = new QHBoxLayout;
    buttons->addWidget(m_discard);
    buttons->addStretch(1);
    buttons->addWidget(m_cancel);
    buttons->addSpacing(12);
    buttons->addWidget(m_copy);
    buttons->addWidget(m_save);
    buttons->addWidget(m_saveCopy);
    root->addLayout(buttons);

    // --- wiring ---
    m_editsTimer.setSingleShot(true);
    m_editsTimer.setInterval(400);
    connect(&m_editsTimer, &QTimer::timeout, this, [this] { emit editsChanged(m_entry.id, currentEdits()); });

    connect(m_cropEdit, &QToolButton::toggled, this, [this](bool on) {
        m_view->setCropEditing(on);
        if(on)
            m_view->setFocus();
    });
    connect(m_cropReset, &QToolButton::clicked, this, [this] {
        setCrop({});
        m_view->setCrop({});
    });
    connect(m_view, &VideoView::cropChanged, this, &ScreenshotEditor::setCrop);
    connect(m_caption, &CaptionBar::changed, this, &ScreenshotEditor::updateOverlay);

    connect(m_save, &QPushButton::clicked, this, [this] { runExport(Action::Save); });
    connect(m_copy, &QPushButton::clicked, this, [this] { runExport(Action::Copy); });
    connect(m_saveCopy, &QPushButton::clicked, this, [this] { runExport(Action::SaveAndCopy); });
    connect(m_cancel, &QPushButton::clicked, this, &ScreenshotEditor::cancel);
    connect(m_discard, &QPushButton::clicked, this, [this] {
        if(QMessageBox::question(this, "Discard screenshot",
                                 "Delete this screenshot? Files already saved are kept.") == QMessageBox::Yes)
        {
            emit discardRequested(m_entry.id);
            close();
        }
    });

    connect(new QShortcut(QKeySequence("Ctrl+S"), this), &QShortcut::activated, this, [this] { runExport(Action::Save); });
    connect(new QShortcut(QKeySequence("Ctrl+C"), this), &QShortcut::activated, this,
            [this] { runExport(Action::SaveAndCopy); });
    connect(new QShortcut(QKeySequence("Ctrl+Shift+C"), this), &QShortcut::activated, this,
            [this] { runExport(Action::Copy); });
    connect(new QShortcut(QKeySequence(Qt::Key_Escape), this), &QShortcut::activated, this, &QWidget::close);

    // --- initial state ---
    m_view->setImage(m_image);
    setCrop({});
    m_initialEdits = entry.edits.value_or(EditState{});
    if(entry.edits)
        restoreEdits(*entry.edits);

    // Size the window around the screenshot's aspect ratio.
    const QSize src = entry.size.isValid() ? entry.size : QSize(16, 9);
    const QSize view = src.scaled(QSize(1100, 660), Qt::KeepAspectRatio).expandedTo(QSize(480, 200));
    resize(std::max(view.width(), 820), view.height() + 120);

    if(m_image.isNull())
    {
        m_status->setText(QStringLiteral("Cannot read the screenshot %1: %2").arg(entry.master, reader.errorString()));
        for(QWidget* w : std::initializer_list<QWidget*>{m_copy, m_save, m_saveCopy, m_cropEdit, m_caption})
            w->setEnabled(false);
    }
}

QSize ScreenshotEditor::outputSize() const
{
    return m_crop.isEmpty() ? m_image.size() : m_crop.size();
}

QImage ScreenshotEditor::composed() const
{
    QImage out = m_crop.isEmpty() ? m_image : m_image.copy(m_crop);
    const TextOverlay overlay = m_caption->overlay();
    if(!overlay.isEmpty())
    {
        QPainter p(&out);
        p.drawImage(0, 0, renderTextOverlay(overlay, out.size()));
    }
    return out;
}

QString ScreenshotEditor::uniqueExportPath() const
{
    const QString dir = m_settings.screenshotDir;
    QString path = QStringLiteral("%1/%2.png").arg(dir, m_entry.id);
    for(int n = 2; QFileInfo::exists(path); ++n)
        path = QStringLiteral("%1/%2-%3.png").arg(dir, m_entry.id).arg(n);
    return path;
}

void ScreenshotEditor::runExport(Action action)
{
    if(m_image.isNull())
        return;
    const QString output = action == Action::Copy ? QStringLiteral("%1/%2.png").arg(Settings::cacheDir(), m_entry.id)
                                                  : uniqueExportPath();
    QDir().mkpath(QFileInfo(output).absolutePath());
    if(!composed().save(output, "PNG"))
    {
        m_status->setText("Could not write " + output);
        emit notify("Save failed", output, true);
        return;
    }
    const QString size = QLocale().formattedDataSize(QFileInfo(output).size(), 1);
    if(action != Action::Copy)
        emit exported(m_entry.id, output);
    if(action == Action::Save)
    {
        m_status->setText(QStringLiteral("Saved %1 (%2)").arg(output, size));
        return;
    }
    copyFileToClipboard(output);
    emit notify("Copied to clipboard", QStringLiteral("%1 (%2)").arg(QFileInfo(output).fileName(), size), false);
    close();
}

void ScreenshotEditor::cancel()
{
    m_editsTimer.stop();
    m_cancelled = true;
    emit editsChanged(m_entry.id, m_initialEdits);
    close();
}

void ScreenshotEditor::updateOverlay()
{
    const TextOverlay o = m_caption->overlay();
    m_view->setOverlay(o.isEmpty() ? QImage() : renderTextOverlay(o, outputSize()));
    m_caption->storeStyle(m_settings);
    m_settings.save();
    scheduleEditsSave();
}

EditState ScreenshotEditor::currentEdits() const
{
    EditState s;
    s.crop = m_crop;
    s.text = m_caption->overlay();
    return s;
}

void ScreenshotEditor::restoreEdits(const EditState& edits)
{
    m_restoring = true;
    m_caption->setOverlay(edits.text);
    setCrop(edits.crop);
    m_view->setCrop(m_crop);
    m_restoring = false;
}

void ScreenshotEditor::scheduleEditsSave()
{
    if(!m_restoring)
        m_editsTimer.start();
}

void ScreenshotEditor::setCrop(const QRect& crop)
{
    // The whole image counts as "no crop".
    m_crop = (crop == QRect(QPoint(0, 0), m_image.size())) ? QRect() : crop;
    m_cropReset->setEnabled(!m_crop.isEmpty());
    const QSize size = outputSize();
    m_cropLabel->setText(m_crop.isEmpty() ? QStringLiteral("%1×%2").arg(size.width()).arg(size.height())
                                          : QStringLiteral("%1×%2 at %3,%4")
                                                .arg(size.width())
                                                .arg(size.height())
                                                .arg(m_crop.x())
                                                .arg(m_crop.y()));
    updateOverlay(); // captions are laid out on the cropped image
}

void ScreenshotEditor::closeEvent(QCloseEvent* e)
{
    if(m_editsTimer.isActive() && !m_cancelled)
    {
        m_editsTimer.stop();
        emit editsChanged(m_entry.id, currentEdits());
    }
    e->accept();
}

} // namespace qr
