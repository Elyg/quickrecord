#pragma once

#include <QWidget>

class QLabel;
class QMediaPlayer;
class QMovie;
class QSlider;
class QToolButton;

namespace qr
{

class VideoView;

// Looping player for the History window: MP4s via QMediaPlayer, GIFs via QMovie, both
// painted by the GPU-free VideoView. PNG screenshots are shown as still images.
class PreviewPane : public QWidget
{
    Q_OBJECT

public:
    explicit PreviewPane(QWidget* parent = nullptr);
    ~PreviewPane() override;

    // No-op if `path` is already showing, so list refreshes don't restart playback.
    void play(const QString& path, const QString& title, const QString& details);
    void clear(const QString& message = {});

    // Pop-out state is owned by the History window; this only updates the button.
    void setDetached(bool detached);

signals:
    void detachToggled(); // pop-out button pressed, or the popped-out window closed

protected:
    void closeEvent(QCloseEvent* e) override;
    // Don't decode frames nobody can see (minimized / hidden window).
    void hideEvent(QHideEvent* e) override;
    void showEvent(QShowEvent* e) override;

private:
    void stopAll();
    void togglePlay();
    void updateControls();
    void seekTo(int value);

    QString m_path;
    QSize m_stillSize; // valid while showing a still image
    bool m_pausedByHide = false;
    VideoView* m_view;
    QMediaPlayer* m_player;
    QMovie* m_movie = nullptr;
    QLabel* m_title;
    QLabel* m_details;
    QToolButton* m_play;
    QSlider* m_seek;
    QLabel* m_time;
    QLabel* m_zoom;
    QToolButton* m_detach;
};

} // namespace qr
