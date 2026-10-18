#pragma once

#include "app/Exporter.h"
#include "core/HistoryStore.h"
#include "core/InputLog.h"
#include "core/TextOverlay.h"

#include <QImage>

#include <QTimer>
#include <QWidget>
#include <optional>
#include <utility>

class QAudioOutput;
class QCheckBox;
class QComboBox;
class QLabel;
class QMediaPlayer;
class QProgressBar;
class QPushButton;
class QSlider;
class QSpinBox;
class QToolButton;

namespace qr
{

struct Settings;
class CaptionBar;
class TrimSlider;
class VideoView;

// Post-record window: loops the master inside the trim range and exports GIF/MP4.
class EditorWindow : public QWidget
{
    Q_OBJECT

public:
    EditorWindow(const HistoryEntry& entry, Settings& settings);
    ~EditorWindow() override;

    QString id() const { return m_entry.id; }

signals:
    void exported(QString id, QString path); // written to the export folder
    void editsChanged(QString id, qr::EditState edits); // debounced; flushed on close
    void discardRequested(QString id);
    void notify(QString title, QString text, bool error);

protected:
    void closeEvent(QCloseEvent*) override;
    void hideEvent(QHideEvent*) override; // pause playback while minimized
    void showEvent(QShowEvent*) override;

private:
    enum class Action
    {
        Save,
        Copy,
        SaveAndCopy,
    };

    Exporter::Format format() const;
    void onFormatChanged();
    void storeFormatDefaults();
    void updateEstimate();
    void updateTimeLabel();
    void runExport(Action action);
    void onExportFinished(bool ok, const QString& output, const QString& error);
    QString uniqueExportPath(const QString& ext) const;
    void setBusy(bool busy);
    QSize outputFrameSize() const; // crop size, or the whole recording
    EditState currentEdits() const;
    void restoreEdits(const EditState& edits);
    void scheduleEditsSave();
    void cancel();
    void setCrop(const QRect& crop);
    void updateOverlay();
    // Keys & clicks at `seconds` into the recording (the frame on screen); re-renders only when
    // what's shown changes.
    void updateInputPreview(double seconds, bool force = false);
    bool inputShown() const;
    InputFrame inputFrame(double seconds) const; // with what the Keys / Clicks boxes hide removed
    // Caption plus `input`, frame-sized, as drawn on the preview and in exports.
    QImage composeOverlay(const InputFrame& input) const;
    // Writes the keys & clicks of the trim range as an image sequence for ffmpeg; its script path.
    QString writeInputSequence(double start, double end);

    HistoryEntry m_entry;
    Settings& m_settings;
    Exporter m_exporter;
    Action m_action = Action::Save;
    bool m_loadingDefaults = false;

    QMediaPlayer* m_player;
    QAudioOutput* m_audio;
    VideoView* m_video;
    TrimSlider* m_trim;
    QToolButton* m_play;
    QLabel* m_time;
    QComboBox* m_format;
    QSpinBox* m_fps;
    QSpinBox* m_width;
    QWidget* m_qualityBox;
    QSlider* m_quality;
    QLabel* m_qualityValue;
    QCheckBox* m_keepAudio;
    CaptionBar* m_caption;
    QToolButton* m_cropEdit;
    QToolButton* m_cropReset;
    QLabel* m_cropLabel;
    QRect m_crop; // empty = whole frame
    QList<InputEvent> m_inputEvents; // recorded keys & clicks (empty: no log, no controls)
    QCheckBox* m_keysBox = nullptr;
    QComboBox* m_inputPosition = nullptr;
    QCheckBox* m_clicksBox = nullptr;
    QImage m_captionImage; // rendered caption, or null
    InputFrame m_shownInput;
    double m_frameTime = 0; // seconds; timestamp of the frame being shown

    // Per-recording edits: restored on open, saved shortly after each change.
    QTimer m_editsTimer;
    bool m_restoring = false;
    bool m_pausedByHide = false;
    EditState m_initialEdits; // what Cancel reverts to
    bool m_cancelled = false;
    std::optional<std::pair<qint64, qint64>> m_pendingTrim; // applied once the real duration is known
    QLabel* m_estimate;
    QProgressBar* m_progress;
    QLabel* m_status;
    QPushButton* m_discard;
    QPushButton* m_cancel;
    QPushButton* m_copy;
    QPushButton* m_save;
    QPushButton* m_saveCopy;
};

} // namespace qr
