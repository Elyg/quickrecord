#include "app/EditorWindow.h"

#include "app/CaptionBar.h"
#include "app/Clipboard.h"
#include "app/FlowLayout.h"
#include "app/Icons.h"
#include "app/Settings.h"
#include "app/TrimSlider.h"
#include "app/VideoView.h"

#include <QAudioOutput>
#include <QCheckBox>
#include <QCloseEvent>
#include <QDir>
#include <QComboBox>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QLocale>
#include <QMediaPlayer>
#include <QPainter>
#include <QProgressBar>
#include <QPushButton>
#include <QShortcut>
#include <QSlider>
#include <QSpinBox>
#include <QToolButton>
#include <QVBoxLayout>
#include <QVideoFrame>
#include <QVideoSink>

namespace qr
{

namespace
{
QString formatTime(qint64 ms)
{
    return QStringLiteral("%1:%2.%3")
        .arg(ms / 60000)
        .arg((ms / 1000) % 60, 2, 10, QLatin1Char('0'))
        .arg((ms / 100) % 10);
}

QLabel* caption(const QString& text, QWidget* parent)
{
    auto* l = new QLabel(text, parent);
    l->setStyleSheet("color: palette(placeholder-text);");
    return l;
}
} // namespace

EditorWindow::EditorWindow(const HistoryEntry& entry, Settings& settings)
    : m_entry(entry)
    , m_settings(settings)
{
    setAttribute(Qt::WA_DeleteOnClose);
    setWindowTitle(QStringLiteral("QuickRecord — %1").arg(entry.id));
    setWindowIcon(icons::app());

    m_player = new QMediaPlayer(this);
    m_audio = new QAudioOutput(this);
    m_player->setAudioOutput(m_audio);
    m_video = new VideoView(this);
    m_video->setMinimumSize(320, 180);
    m_player->setVideoSink(m_video->sink());

    m_play = new QToolButton(this);
    m_play->setText("❚❚");
    m_play->setToolTip("Play / pause (Space)");
    m_trim = new TrimSlider(this);
    m_time = new QLabel(this);
    m_time->setMinimumWidth(170);

    m_format = new QComboBox(this);
    m_format->addItem("GIF", "gif");
    m_format->addItem("MP4", "mp4");
    m_fps = new QSpinBox(this);
    m_width = new QSpinBox(this);
    m_width->setRange(0, 7680);
    m_width->setSingleStep(20);
    m_width->setSpecialValueText("Original");
    m_width->setSuffix(" px");

    m_qualityBox = new QWidget(this);
    auto* ql = new QHBoxLayout(m_qualityBox);
    ql->setContentsMargins(0, 0, 0, 0);
    m_quality = new QSlider(Qt::Horizontal, m_qualityBox);
    m_quality->setRange(1, 100);
    m_quality->setFixedWidth(110);
    m_qualityValue = new QLabel(m_qualityBox);
    m_qualityValue->setMinimumWidth(24);
    ql->addWidget(caption("Quality", m_qualityBox));
    ql->addWidget(m_quality);
    ql->addWidget(m_qualityValue);

    m_keepAudio = new QCheckBox("Audio", this);
    m_keepAudio->setVisible(entry.hasAudio);

    m_caption = new CaptionBar(settings, "Font size in pixels of the recording; it scales with the export width", this);

    // --- crop ---
    m_cropEdit = new QToolButton(this);
    m_cropEdit->setText("Crop");
    m_cropEdit->setCheckable(true);
    m_cropEdit->setToolTip("Drag the handles on the preview; drag outside the box to draw a new one");
    m_cropReset = new QToolButton(this);
    m_cropReset->setText("Reset");
    m_cropReset->setToolTip("Use the whole frame");
    m_cropLabel = new QLabel(this);

    m_estimate = new QLabel(this);
    m_progress = new QProgressBar(this);
    m_progress->setRange(0, 1000);
    m_progress->setTextVisible(false);
    m_progress->setFixedHeight(8);
    m_progress->hide();
    m_status = new QLabel(this);
    m_status->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_status->setWordWrap(true);

    m_cancelExport = new QPushButton("Cancel export", this);
    m_cancelExport->setToolTip("Stop this export");
    m_cancelExport->hide();
    m_close = new QPushButton("Close", this);
    m_close->setToolTip("Close (Esc); the trim, crop and text stay for next time");
    m_save = new QPushButton("Save && Close", this);
    m_save->setToolTip("Export the edited GIF / MP4 next to the recording and close (Ctrl+S); without edits it "
                       "just closes");
    m_copy = new QPushButton("Copy", this);
    m_copy->setToolTip("Export the edited GIF / MP4, copy it to the clipboard and close (Ctrl+C); without edits it "
                       "copies the recording");
    m_copy->setDefault(true);

    auto* root = new QVBoxLayout(this);
    root->addWidget(m_video, 1);

    auto* timeline = new QHBoxLayout;
    timeline->addWidget(m_play);
    timeline->addWidget(m_trim, 1);
    timeline->addWidget(m_time);
    root->addLayout(timeline);

    // These rows wrap when the window is narrow; each label stays with its field.
    auto* opts = new FlowLayout(nullptr, 12);
    opts->addLayout(group({caption("Format", this), m_format}));
    opts->addLayout(group({caption("FPS", this), m_fps}));
    opts->addLayout(group({caption("Max width", this), m_width}));
    opts->addWidget(m_qualityBox);
    opts->addWidget(m_keepAudio);
    opts->addWidget(m_estimate);
    root->addLayout(opts);

    auto* textRow = new FlowLayout(nullptr, 16);
    textRow->addLayout(group({m_cropEdit, m_cropReset, m_cropLabel}));
    textRow->addWidget(m_caption);
    m_inputEvents = loadInputLog(inputLogPath(Settings::dataDir(), entry.id));
    if(!m_inputEvents.isEmpty())
    {
        m_keysBox = new QCheckBox("Keys", this);
        m_keysBox->setChecked(settings.inputKeys);
        m_keysBox->setToolTip("Show the keys pressed during the recording, in the preview and the export");
        m_clicksBox = new QCheckBox("Clicks", this);
        m_clicksBox->setChecked(settings.inputClicks);
        m_clicksBox->setToolTip("Show a ring where the mouse was clicked, in the preview and the export");
        m_inputPosition = new QComboBox(this);
        for(const auto& [label, pos] : std::initializer_list<std::pair<const char*, TextPosition>>{
                {"Bottom left", TextPosition::BottomLeft},
                {"Bottom center", TextPosition::BottomCenter},
                {"Bottom right", TextPosition::BottomRight},
                {"Top left", TextPosition::TopLeft},
                {"Top center", TextPosition::TopCenter},
                {"Top right", TextPosition::TopRight}})
            m_inputPosition->addItem(label, toString(pos));
        m_inputPosition->setCurrentIndex(std::max(0, m_inputPosition->findData(settings.inputPosition)));
        m_inputPosition->setToolTip("Where the keys are shown");
        textRow->addLayout(group({m_keysBox, m_inputPosition, m_clicksBox}, 8));
    }
    root->addLayout(textRow);

    root->addWidget(m_progress);
    root->addWidget(m_status);

    auto* buttons = new QHBoxLayout;
    buttons->addWidget(m_cancelExport);
    buttons->addStretch(1);
    buttons->addWidget(m_close);
    buttons->addSpacing(12);
    buttons->addWidget(m_save);
    buttons->addWidget(m_copy);
    root->addLayout(buttons);

    // --- wiring ---
    connect(m_play, &QToolButton::clicked, this, [this] {
        if(m_player->playbackState() == QMediaPlayer::PlayingState)
            m_player->pause();
        else
            m_player->play();
    });
    connect(m_player, &QMediaPlayer::playbackStateChanged, this,
            [this](QMediaPlayer::PlaybackState s) { m_play->setText(s == QMediaPlayer::PlayingState ? "❚❚" : "▶"); });
    connect(m_player, &QMediaPlayer::durationChanged, this, [this](qint64 ms) {
        if(ms <= 0)
            return;
        m_trim->setDuration(ms);
        if(m_pendingTrim)
        {
            const auto [start, end] = *m_pendingTrim;
            m_pendingTrim.reset();
            m_trim->setRange(start, end > 0 ? end : ms);
        }
    });
    connect(m_player, &QMediaPlayer::positionChanged, this, [this](qint64 ms) {
        m_trim->setPosition(ms);
        if(m_trim->end() > 0 && ms >= m_trim->end() && m_player->playbackState() == QMediaPlayer::PlayingState)
            m_player->setPosition(m_trim->start());
    });
    connect(m_player, &QMediaPlayer::mediaStatusChanged, this, [this](QMediaPlayer::MediaStatus s) {
        if(s == QMediaPlayer::EndOfMedia)
        {
            m_player->setPosition(m_trim->start());
            m_player->play();
        }
    });
    connect(m_player, &QMediaPlayer::errorOccurred, this,
            [this](QMediaPlayer::Error, const QString& msg) { m_status->setText("Preview unavailable: " + msg); });
    connect(m_trim, &TrimSlider::seekRequested, m_player, &QMediaPlayer::setPosition);
    connect(m_trim, &TrimSlider::rangeChanged, this, [this] {
        updateTimeLabel();
        updateEstimate();
        scheduleEditsSave();
    });
    m_editsTimer.setSingleShot(true);
    m_editsTimer.setInterval(400);
    connect(&m_editsTimer, &QTimer::timeout, this, [this] { emit editsChanged(m_entry.id, currentEdits()); });

    connect(m_format, &QComboBox::currentIndexChanged, this, &EditorWindow::onFormatChanged);
    for(QSpinBox* s : {m_fps, m_width})
        connect(s, &QSpinBox::valueChanged, this, [this] {
            storeFormatDefaults();
            updateEstimate();
        });
    connect(m_quality, &QSlider::valueChanged, this, [this](int v) {
        m_qualityValue->setText(QString::number(v));
        storeFormatDefaults();
        updateEstimate();
    });
    connect(m_keepAudio, &QCheckBox::toggled, this, [this] { storeFormatDefaults(); });

    connect(m_cropEdit, &QToolButton::toggled, this, [this](bool on) {
        m_video->setCropEditing(on);
        if(on)
            m_video->setFocus();
    });
    connect(m_cropReset, &QToolButton::clicked, this, [this] {
        setCrop({});
        m_video->setCrop({});
    });
    connect(m_video, &VideoView::cropChanged, this, &EditorWindow::setCrop);

    connect(m_caption, &CaptionBar::changed, this, &EditorWindow::updateOverlay);
    if(m_keysBox)
    {
        auto changed = [this] {
            m_settings.inputKeys = m_keysBox->isChecked();
            m_settings.inputClicks = m_clicksBox->isChecked();
            m_settings.inputPosition = m_inputPosition->currentData().toString();
            m_settings.save();
            m_inputPosition->setEnabled(m_keysBox->isChecked());
            updateInputPreview(m_frameTime, true);
        };
        connect(m_keysBox, &QCheckBox::toggled, this, changed);
        connect(m_clicksBox, &QCheckBox::toggled, this, changed);
        connect(m_inputPosition, &QComboBox::currentIndexChanged, this, changed);
        m_inputPosition->setEnabled(m_keysBox->isChecked());
        // Follow the frame actually on screen; the player's position reports are too coarse.
        connect(m_video->sink(), &QVideoSink::videoFrameChanged, this, [this](const QVideoFrame& frame) {
            if(frame.isValid() && frame.startTime() >= 0)
                updateInputPreview(frame.startTime() / 1e6);
        });
    }

    connect(m_cancelExport, &QPushButton::clicked, this, &EditorWindow::cancelExport);
    connect(m_close, &QPushButton::clicked, this, &QWidget::close);
    connect(m_save, &QPushButton::clicked, this, [this] { runExport(Action::Save); });
    connect(m_copy, &QPushButton::clicked, this, [this] { runExport(Action::Copy); });
    connect(new QShortcut(QKeySequence("Ctrl+S"), this), &QShortcut::activated, this, [this] { runExport(Action::Save); });
    connect(new QShortcut(QKeySequence("Ctrl+C"), this), &QShortcut::activated, this, [this] { runExport(Action::Copy); });
    connect(new QShortcut(QKeySequence(Qt::Key_Space), this), &QShortcut::activated, m_play, &QToolButton::click);
    connect(new QShortcut(QKeySequence(Qt::Key_Escape), this), &QShortcut::activated, this, &QWidget::close);

    connect(&m_exporter, &Exporter::progress, this, [this](double f) { m_progress->setValue(int(f * 1000)); });
    connect(&m_exporter, &Exporter::finished, this, &EditorWindow::onExportFinished);

    // --- initial state ---
    setCrop({});
    m_format->setCurrentIndex(m_settings.lastFormat == "mp4" ? 1 : 0);
    onFormatChanged();
    if(entry.duration > 0)
        m_trim->setDuration(qint64(entry.duration * 1000));
    if(entry.edits)
        restoreEdits(*entry.edits);

    // Size the window around the video's aspect ratio.
    const QSize src = entry.size.isValid() ? entry.size : QSize(16, 9);
    const QSize video = src.scaled(QSize(960, 540), Qt::KeepAspectRatio).expandedTo(QSize(480, 200));
    resize(std::max(video.width(), 760), video.height() + 190);

    if(QFileInfo::exists(entry.master))
    {
        m_player->setSource(QUrl::fromLocalFile(entry.master));
        m_player->play();
    }
    else
    {
        m_status->setText("The master recording is missing: " + entry.master);
        for(QPushButton* b : {m_copy, m_save})
            b->setEnabled(false);
    }
}

EditorWindow::~EditorWindow() = default;

Exporter::Format EditorWindow::format() const
{
    return m_format->currentData().toString() == "mp4" ? Exporter::Format::Mp4 : Exporter::Format::Gif;
}

void EditorWindow::onFormatChanged()
{
    m_loadingDefaults = true;
    const bool gif = format() == Exporter::Format::Gif;
    m_settings.lastFormat = gif ? "gif" : "mp4";
    if(gif)
    {
        m_fps->setRange(1, 50);
        m_fps->setSpecialValueText({});
        m_fps->setValue(m_settings.gifFps);
        m_width->setValue(m_settings.gifMaxWidth);
        m_quality->setValue(m_settings.gifQuality);
        m_qualityValue->setText(QString::number(m_settings.gifQuality));
    }
    else
    {
        m_fps->setRange(0, 60);
        m_fps->setSpecialValueText("Source");
        m_fps->setValue(m_settings.mp4Fps);
        m_width->setValue(m_settings.mp4MaxWidth);
        m_keepAudio->setChecked(m_settings.mp4Audio);
    }
    m_qualityBox->setVisible(gif);
    m_keepAudio->setVisible(!gif && m_entry.hasAudio);
    m_loadingDefaults = false;
    m_settings.save();
    updateEstimate();
}

void EditorWindow::storeFormatDefaults()
{
    if(m_loadingDefaults)
        return;
    if(format() == Exporter::Format::Gif)
    {
        m_settings.gifFps = m_fps->value();
        m_settings.gifMaxWidth = m_width->value();
        m_settings.gifQuality = m_quality->value();
    }
    else
    {
        m_settings.mp4Fps = m_fps->value();
        m_settings.mp4MaxWidth = m_width->value();
        m_settings.mp4Audio = m_keepAudio->isChecked();
    }
    m_settings.save();
}

void EditorWindow::updateTimeLabel()
{
    const qint64 len = m_trim->end() - m_trim->start();
    m_time->setText(QStringLiteral("%1 – %2  (%3 s)")
                        .arg(formatTime(m_trim->start()), formatTime(m_trim->end()))
                        .arg(len / 1000.0, 0, 'f', 1));
}

void EditorWindow::updateEstimate()
{
    // Deliberately rough: screen content compresses very differently depending on motion.
    const double seconds = (m_trim->end() - m_trim->start()) / 1000.0;
    const QSize src = outputFrameSize();
    if(seconds <= 0 || !src.isValid())
    {
        m_estimate->clear();
        return;
    }
    const int w = outputWidth(src.width(), m_width->value());
    const double h = double(src.height()) * w / src.width();
    const bool gif = format() == Exporter::Format::Gif;
    const int fps = m_fps->value() > 0 ? m_fps->value() : m_settings.fps;
    const double pixels = w * h * fps * seconds;
    const double bytes = gif ? pixels * 0.06 * (m_quality->value() / 90.0) : pixels * 0.002;
    m_estimate->setText(QStringLiteral("%1×%2 · ≈ %3")
                            .arg(w)
                            .arg(int(h) & ~1)
                            .arg(QLocale().formattedDataSize(qint64(bytes), 1)));
}

bool EditorWindow::hasEdits() const
{
    if(!currentEdits().isDefault() || inputShown() || format() == Exporter::Format::Gif)
        return true;
    return m_fps->value() != 0 || m_width->value() != 0 || (m_entry.hasAudio && !m_keepAudio->isChecked());
}

void EditorWindow::runExport(Action action)
{
    if(m_exporter.isRunning() || !QFileInfo::exists(m_entry.master))
        return;
    if(!hasEdits())
    {
        if(action == Action::Copy)
        {
            copyFileToClipboard(m_entry.master);
            emit notify("Copied to clipboard",
                        QStringLiteral("%1 (%2)").arg(QFileInfo(m_entry.master).fileName(),
                                                      QLocale().formattedDataSize(QFileInfo(m_entry.master).size(), 1)),
                        false);
        }
        close();
        return;
    }
    m_action = action;
    const bool gif = format() == Exporter::Format::Gif;
    const QString ext = gif ? "gif" : "mp4";

    ExportParams p;
    p.input = m_entry.master;
    p.start = m_trim->start() / 1000.0;
    p.end = m_trim->end() / 1000.0;
    p.fps = m_fps->value();
    p.sourceWidth = m_entry.size.width();
    p.crop = m_crop;
    p.maxWidth = m_width->value();
    p.gifQuality = m_quality->value();
    p.keepAudio = !gif && m_entry.hasAudio && m_keepAudio->isChecked();
    const TextOverlay overlay = m_caption->overlay();
    if(inputShown())
    {
        // Keys & clicks change over time: an image sequence (with the caption in every frame).
        p.overlaySequence = writeInputSequence(p.start, p.end);
        if(p.overlaySequence.isEmpty())
            return;
    }
    else if(!overlay.isEmpty())
    {
        // Rendered at the recording's size and composited before scaling, exactly as previewed.
        QDir().mkpath(Settings::cacheDir());
        p.overlayImage = QStringLiteral("%1/%2-text.png").arg(Settings::cacheDir(), m_entry.id);
        if(!renderTextOverlay(overlay, outputFrameSize()).save(p.overlayImage))
        {
            m_status->setText("Could not write " + p.overlayImage);
            return;
        }
    }
    p.output = editedPath(m_entry, ext); // replaced only once the new one is complete

    setBusy(true);
    m_status->setText(QStringLiteral("Exporting %1…").arg(ext.toUpper()));
    m_exporter.start(format(), p);
}

void EditorWindow::onExportFinished(bool ok, const QString& output, const QString& error)
{
    setBusy(false);
    if(!ok)
    {
        m_status->setText("Export failed:\n" + error);
        emit notify("Export failed", error, true);
        return;
    }
    emit exported(m_entry.id, output);
    if(m_action == Action::Copy)
    {
        copyFileToClipboard(output);
        emit notify("Copied to clipboard",
                    QStringLiteral("%1 (%2)").arg(QFileInfo(output).fileName(),
                                                  QLocale().formattedDataSize(QFileInfo(output).size(), 1)),
                    false);
    }
    close();
}

void EditorWindow::cancelExport()
{
    if(!m_exporter.isRunning())
        return;
    m_exporter.cancel(); // also removes the partial file
    setBusy(false);
    m_status->setText("Export cancelled.");
}

void EditorWindow::setBusy(bool busy)
{
    m_cancelExport->setVisible(busy);
    m_progress->setVisible(busy);
    m_progress->setValue(0);
    for(QWidget* w : std::initializer_list<QWidget*>{m_copy, m_save, m_format, m_fps, m_width, m_qualityBox,
                                                     m_keepAudio, m_caption, m_cropEdit, m_cropReset})
        w->setEnabled(!busy);
}

void EditorWindow::updateOverlay()
{
    const TextOverlay o = m_caption->overlay();
    m_captionImage = o.isEmpty() ? QImage() : renderTextOverlay(o, outputFrameSize());
    updateInputPreview(m_frameTime, true);

    m_caption->storeStyle(m_settings);
    m_settings.save();
    scheduleEditsSave();
}

EditState EditorWindow::currentEdits() const
{
    EditState s;
    if(m_pendingTrim) // media not loaded yet: keep what was saved
        std::tie(s.trimStartMs, s.trimEndMs) = *m_pendingTrim;
    else
    {
        s.trimStartMs = m_trim->start();
        s.trimEndMs = m_trim->end() >= m_trim->duration() ? 0 : m_trim->end();
    }
    s.crop = m_crop;
    s.text = m_caption->overlay();
    return s;
}

void EditorWindow::restoreEdits(const EditState& edits)
{
    m_restoring = true;
    m_caption->setOverlay(edits.text);
    setCrop(edits.crop);
    m_video->setCrop(m_crop);
    if(edits.trimStartMs > 0 || edits.trimEndMs > 0)
        m_pendingTrim = std::make_pair(edits.trimStartMs, edits.trimEndMs);
    m_restoring = false;
}

void EditorWindow::scheduleEditsSave()
{
    if(!m_restoring)
        m_editsTimer.start();
}

bool EditorWindow::inputShown() const
{
    return m_keysBox && (m_keysBox->isChecked() || m_clicksBox->isChecked());
}

InputFrame EditorWindow::inputFrame(double seconds) const
{
    InputFrame f = inputFrameAt(m_inputEvents, seconds);
    if(!m_keysBox->isChecked())
        f.keys.clear();
    if(!m_clicksBox->isChecked())
        f.rings.clear();
    return f;
}

QImage EditorWindow::composeOverlay(const InputFrame& input) const
{
    QImage img = m_captionImage.isNull() ? QImage() : m_captionImage.copy();
    if(img.isNull())
    {
        img = QImage(outputFrameSize(), QImage::Format_ARGB32_Premultiplied);
        img.fill(Qt::transparent);
    }
    if(!input.isEmpty())
    {
        QPainter p(&img);
        p.drawImage(0, 0,
                    renderInputOverlay(input, img.size(), textPositionFromString(m_inputPosition->currentData().toString()),
                                       m_crop.topLeft()));
    }
    return img;
}

void EditorWindow::updateInputPreview(double seconds, bool force)
{
    m_frameTime = seconds;
    const InputFrame frame = inputShown() ? inputFrame(seconds) : InputFrame();
    if(!force && frame == m_shownInput)
        return;
    m_shownInput = frame;
    m_video->setOverlay(frame.isEmpty() ? m_captionImage : composeOverlay(frame));
}

QString EditorWindow::writeInputSequence(double start, double end)
{
    QString error;
    const QString list = writeOverlaySequence(
        inputFrameTimes(m_inputEvents, start, end), end,
        QStringLiteral("%1/%2-overlay").arg(Settings::cacheDir(), m_entry.id),
        [this](double t) { return inputFrame(t); }, [this](const InputFrame& f) { return composeOverlay(f); }, &error);
    if(list.isEmpty())
        m_status->setText(error);
    return list;
}

QSize EditorWindow::outputFrameSize() const
{
    return m_crop.isEmpty() ? m_entry.size : m_crop.size();
}

void EditorWindow::setCrop(const QRect& crop)
{
    // The whole frame counts as "no crop".
    m_crop = (crop == QRect(QPoint(0, 0), m_entry.size)) ? QRect() : crop;
    m_cropReset->setEnabled(!m_crop.isEmpty());
    m_cropLabel->setText(m_crop.isEmpty()
                             ? QStringLiteral("%1×%2").arg(m_entry.size.width()).arg(m_entry.size.height())
                             : QStringLiteral("%1×%2 at %3,%4")
                                   .arg(m_crop.width())
                                   .arg(m_crop.height())
                                   .arg(m_crop.x())
                                   .arg(m_crop.y()));
    updateOverlay(); // captions are laid out on the cropped frame
    updateEstimate();
    scheduleEditsSave();
}

void EditorWindow::hideEvent(QHideEvent* e)
{
    QWidget::hideEvent(e);
    if(m_player->playbackState() == QMediaPlayer::PlayingState)
    {
        m_pausedByHide = true;
        m_player->pause();
    }
}

void EditorWindow::showEvent(QShowEvent* e)
{
    QWidget::showEvent(e);
    if(m_pausedByHide)
    {
        m_pausedByHide = false;
        m_player->play();
    }
}

void EditorWindow::closeEvent(QCloseEvent* e)
{
    if(m_editsTimer.isActive())
    {
        m_editsTimer.stop();
        emit editsChanged(m_entry.id, currentEdits());
    }
    m_exporter.cancel();
    m_player->stop();
    e->accept();
}

} // namespace qr
