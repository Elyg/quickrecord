#pragma once

#include "core/TextOverlay.h"

#include <QDateTime>
#include <QList>
#include <QRect>
#include <QSize>
#include <QString>
#include <QStringList>
#include <optional>

namespace qr
{

// The editor state of one recording or screenshot, so reopening it continues where you
// left off. Screenshots use only crop and text.
struct EditState
{
    qint64 trimStartMs = 0;
    qint64 trimEndMs = 0; // 0 = until the end
    QRect crop;           // empty = whole frame
    TextOverlay text;     // caption, including its style

    bool isDefault() const { return trimStartMs == 0 && trimEndMs == 0 && crop.isEmpty() && text.isEmpty(); }
    bool operator==(const EditState&) const = default;
};

enum class EntryKind
{
    Recording,
    Screenshot,
};

struct HistoryEntry
{
    EntryKind kind = EntryKind::Recording;
    QString id;          // "qr-20261009-153012" (recording) / "qs-20261009-153012" (screenshot)
    QDateTime created;
    QString master;      // absolute path to the MP4 master, or the original PNG of a screenshot
    QString thumb;       // absolute path to the sprite-sheet PNG, or a screenshot's thumbnail
    double duration = 0; // seconds; 0 for screenshots
    QSize size;          // physical capture size
    bool hasAudio = false;
    QStringList exports; // absolute paths, oldest first
    std::optional<EditState> edits; // nullopt = never edited
};

// The recordings and screenshots index (index.json). Newest entries first. Keeps everything; deleting is
// always explicit.
class HistoryStore
{
public:
    explicit HistoryStore(QString indexPath);

    bool load();
    bool save() const;

    const QList<HistoryEntry>& entries() const { return m_entries; }
    const HistoryEntry* find(const QString& id) const;

    void add(const HistoryEntry& entry);
    bool remove(const QString& id);
    bool addExport(const QString& id, const QString& path);
    // Forgets an export path wherever it is listed. True if any entry referenced it.
    bool removeExport(const QString& path);
    bool setThumb(const QString& id, const QString& path);
    // Default edits are stored as "none".
    bool setEdits(const QString& id, const EditState& edits);

private:
    HistoryEntry* findMutable(const QString& id);

    QString m_indexPath;
    QList<HistoryEntry> m_entries;
};

} // namespace qr
