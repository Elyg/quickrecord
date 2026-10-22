#pragma once

#include <QWidget>

class QButtonGroup;
class QHBoxLayout;
class QLabel;
class QMediaPlayer;
class QMovie;
class QSlider;
class QToolButton;

namespace qr
{

class VideoView;

// Looping player for the History window: MP4s via QMediaPlayer, GIFs via QMovie, both
// painted by the GPU-free VideoView. PNG screenshots are shown as still images, without the
// playback controls. A segmented switch picks between versions of one item (Source / Edited).
class PreviewPane : public QWidget
{
    Q_OBJECT

public:
    explicit PreviewPane(QWidget* parent = nullptr);
    ~PreviewPane() override;

    // No-op if `path` is already showing, so list refreshes don't restart playback.
    void play(const QString& path, const QString& title, const QString& details);
    // A rendered image (e.g. the contact sheet a selection would make).
    void showImage(const QImage& image, const QString& title, const QString& details);
    void clear(const QString& message = {});

    // The switch's buttons; none for fewer than two. `current` is checked.
    void setVariants(const QStringList& labels, int current);

    // Pop-out state is owned by the History window; this only updates the button.
    void setDetached(bool detached);

signals:
    void detachToggled(); // pop-out button pressed, or the popped-out window closed
    void variantChosen(int index);

protected:
    void closeEvent(QCloseEvent* e) override;
    // Don't decode frames nobody can see (minimized / hidden window).
    void hideEvent(QHideEvent* e) override;
    void showEvent(QShowEvent* e) override;

private:
    void stopAll();
    void setDetails(const QString& details);
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
    QWidget* m_variants;
    QHBoxLayout* m_variantLayout;
    QButtonGroup* m_variantGroup;
};

} // namespace qr
