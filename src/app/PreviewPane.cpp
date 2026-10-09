#include "app/PreviewPane.h"

#include "app/VideoView.h"

#include <QCloseEvent>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QImageReader>
#include <QLabel>
#include <QMediaPlayer>
#include <QMovie>
#include <QSlider>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>

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
} // namespace

PreviewPane::PreviewPane(QWidget* parent)
    : QWidget(parent)
    , m_view(new VideoView(this))
    , m_player(new QMediaPlayer(this))
{
    m_view->setMinimumSize(320, 200);
    m_player->setVideoSink(m_view->sink());
    m_player->setLoops(QMediaPlayer::Infinite);

    m_title = new QLabel(this);
    QFont f = m_title->font();
    f.setBold(true);
    m_title->setFont(f);
    m_title->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_details = new QLabel(this);
    m_details->setStyleSheet("color: palette(placeholder-text);");
    m_details->setWordWrap(true);

    m_play = new QToolButton(this);
    m_play->setToolTip("Play / pause");
    m_seek = new QSlider(Qt::Horizontal, this);
    m_time = new QLabel(this);
    m_time->setMinimumWidth(110);

    auto tool = [this](const QString& text, const QString& tip) {
        auto* b = new QToolButton(this);
        b->setText(text);
        b->setToolTip(tip);
        return b;
    };
    auto* fit = tool("Fit", "Fit to the pane (0, or double-click)");
    auto* actual = tool("1:1", "Actual pixels (1, or double-click)");
    auto* zoomOut = tool("−", "Zoom out (−, or mouse wheel)");
    auto* zoomIn = tool("+", "Zoom in (+, or mouse wheel) — drag to pan when zoomed");
    m_zoom = new QLabel(this);
    m_zoom->setMinimumWidth(48);
    m_zoom->setAlignment(Qt::AlignCenter);
    m_detach = tool("⧉", {});
    setDetached(false);

    auto* controls = new QHBoxLayout;
    controls->addWidget(m_play);
    controls->addWidget(m_seek, 1);
    controls->addWidget(m_time);
    controls->addSpacing(8);
    controls->addWidget(fit);
    controls->addWidget(actual);
    controls->addWidget(zoomOut);
    controls->addWidget(m_zoom);
    controls->addWidget(zoomIn);
    controls->addSpacing(8);
    controls->addWidget(m_detach);

    connect(fit, &QToolButton::clicked, m_view, &VideoView::fitToView);
    connect(actual, &QToolButton::clicked, m_view, &VideoView::setActualSize);
    connect(zoomOut, &QToolButton::clicked, this, [this] { m_view->zoomBy(1 / 1.25); });
    connect(zoomIn, &QToolButton::clicked, this, [this] { m_view->zoomBy(1.25); });
    connect(m_view, &VideoView::zoomChanged, this, [this](qreal scale, bool fit) {
        m_zoom->setText(QStringLiteral("%1%2%").arg(fit ? "fit " : "").arg(qRound(scale * 100)));
    });
    connect(m_detach, &QToolButton::clicked, this, &PreviewPane::detachToggled);

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->addWidget(m_view, 1);
    root->addLayout(controls);
    root->addWidget(m_title);
    root->addWidget(m_details);

    connect(m_play, &QToolButton::clicked, this, &PreviewPane::togglePlay);
    connect(m_seek, &QSlider::sliderMoved, this, &PreviewPane::seekTo);
    connect(m_player, &QMediaPlayer::playbackStateChanged, this, &PreviewPane::updateControls);
    connect(m_player, &QMediaPlayer::durationChanged, this, &PreviewPane::updateControls);
    connect(m_player, &QMediaPlayer::positionChanged, this, &PreviewPane::updateControls);
    connect(m_player, &QMediaPlayer::errorOccurred, this,
            [this](QMediaPlayer::Error, const QString& msg) { m_details->setText("Cannot play: " + msg); });

    clear("Select a recording or export to preview it.");
}

PreviewPane::~PreviewPane()
{
    stopAll();
}

void PreviewPane::stopAll()
{
    m_stillSize = {};
    m_player->stop();
    m_player->setSource({});
    if(m_movie)
    {
        m_movie->stop();
        m_movie->deleteLater();
        m_movie = nullptr;
    }
}

void PreviewPane::clear(const QString& message)
{
    stopAll();
    m_path.clear();
    m_view->clear();
    m_title->setText(message);
    m_details->clear();
    updateControls();
}

void PreviewPane::play(const QString& path, const QString& title, const QString& details)
{
    if(path == m_path)
        return;
    stopAll();
    m_view->clear();
    m_path = path;
    m_title->setText(title);
    m_details->setText(details);

    if(!QFileInfo::exists(path))
    {
        m_details->setText("File not found: " + path);
        updateControls();
        return;
    }

    if(path.endsWith(".png", Qt::CaseInsensitive))
    {
        QImageReader reader(path);
        const QImage image = reader.read();
        if(image.isNull())
            m_details->setText("Cannot show: " + reader.errorString());
        m_stillSize = image.size();
        m_view->setImage(image);
    }
    else if(path.endsWith(".gif", Qt::CaseInsensitive))
    {
        m_movie = new QMovie(path, {}, this);
        m_movie->setCacheMode(QMovie::CacheAll); // makes seeking work
        connect(m_movie, &QMovie::frameChanged, this, [this] {
            if(m_movie)
            {
                m_view->setImage(m_movie->currentImage());
                updateControls();
            }
        });
        connect(m_movie, &QMovie::stateChanged, this, &PreviewPane::updateControls);
        m_movie->start();
    }
    else
    {
        m_player->setSource(QUrl::fromLocalFile(path));
        m_player->play();
    }
    updateControls();
}

void PreviewPane::setDetached(bool detached)
{
    m_detach->setToolTip(detached ? "Put the preview back into the History window"
                                  : "Pop the preview out into its own resizable window");
    m_detach->setText(detached ? "⇲" : "⧉");
    setWindowTitle(QStringLiteral("QuickRecord — Preview"));
}

void PreviewPane::closeEvent(QCloseEvent* e)
{
    // Closing the popped-out window docks it back instead of destroying it.
    if(isWindow() && parentWidget() == nullptr)
    {
        e->ignore();
        emit detachToggled();
        return;
    }
    QWidget::closeEvent(e);
}

void PreviewPane::hideEvent(QHideEvent* e)
{
    QWidget::hideEvent(e);
    const bool playing = m_movie ? m_movie->state() == QMovie::Running
                                 : m_player->playbackState() == QMediaPlayer::PlayingState;
    if(!playing)
        return;
    m_pausedByHide = true;
    if(m_movie)
        m_movie->setPaused(true);
    else
        m_player->pause();
}

void PreviewPane::showEvent(QShowEvent* e)
{
    QWidget::showEvent(e);
    if(!m_pausedByHide)
        return;
    m_pausedByHide = false;
    if(m_movie)
        m_movie->setPaused(false);
    else
        m_player->play();
}

void PreviewPane::togglePlay()
{
    if(m_movie)
        m_movie->setPaused(m_movie->state() == QMovie::Running);
    else if(m_player->playbackState() == QMediaPlayer::PlayingState)
        m_player->pause();
    else if(!m_path.isEmpty())
        m_player->play();
}

void PreviewPane::seekTo(int value)
{
    if(m_movie)
        m_movie->jumpToFrame(value);
    else
        m_player->setPosition(value);
}

void PreviewPane::updateControls()
{
    bool playing = false;
    if(m_stillSize.isValid())
    {
        m_seek->setRange(0, 0);
        m_time->setText(QStringLiteral("%1 × %2").arg(m_stillSize.width()).arg(m_stillSize.height()));
    }
    else if(m_movie)
    {
        playing = m_movie->state() == QMovie::Running;
        const int frames = std::max(1, m_movie->frameCount());
        m_seek->setRange(0, frames - 1);
        if(!m_seek->isSliderDown())
            m_seek->setValue(m_movie->currentFrameNumber());
        m_time->setText(QStringLiteral("frame %1 / %2").arg(m_movie->currentFrameNumber() + 1).arg(frames));
    }
    else
    {
        playing = m_player->playbackState() == QMediaPlayer::PlayingState;
        m_seek->setRange(0, int(m_player->duration()));
        if(!m_seek->isSliderDown())
            m_seek->setValue(int(m_player->position()));
        m_time->setText(m_path.isEmpty() ? QString()
                                         : formatTime(m_player->position()) + " / " + formatTime(m_player->duration()));
    }
    m_play->setText(playing ? "❚❚" : "▶");
    m_play->setEnabled(!m_path.isEmpty() && !m_stillSize.isValid());
    m_seek->setEnabled(!m_path.isEmpty() && !m_stillSize.isValid());
}

} // namespace qr
