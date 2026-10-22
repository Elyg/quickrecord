#pragma once

#include "core/Annotation.h"
#include "core/Sheet.h"
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
// left off. Screenshots use crop, text and annotations; recordings trim, crop and text.
struct EditState
{
    qint64 trimStartMs = 0;
    qint64 trimEndMs = 0; // 0 = until the end
    QRect crop;           // empty = whole frame
    TextOverlay text;     // caption, including its style
    QList<Annotation> annotations; // drawn on a screenshot, in order

    bool isDefault() const
    {
        return trimStartMs == 0 && trimEndMs == 0 && crop.isEmpty() && text.isEmpty() && annotations.isEmpty();
    }
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
    // "qr-20261009-153012" (recording) / "qs-20261009-153012" (screenshot) / "qsheet-20261009-153012"
    QString id;
    QDateTime created;
    QString master;      // absolute path to the MP4 master, the original PNG of a screenshot, or a sheet's PNG
    QString thumb;       // absolute path to the sprite-sheet PNG, or a screenshot's thumbnail
    double duration = 0; // seconds; 0 for screenshots
    QSize size;          // physical capture size
    bool hasAudio = false;
    QStringList exports; // the edited files (at most one per extension), newest last
    std::optional<EditState> edits; // nullopt = never edited
    std::optional<SheetState> sheet; // set on a contact sheet (kind Screenshot; master is its PNG)
};

// Where the edited `ext` file of `entry` goes: "<master's folder>/<id>-edited.<ext>".
QString editedPath(const HistoryEntry& entry, const QString& ext);
// The edited file with extension `ext` ("gif"), or the newest one, if it still exists; else "".
QString editedFor(const HistoryEntry& entry, const QString& ext);
QString latestEdited(const HistoryEntry& entry);

// The recordings and screenshots index (index.json). Newest entries first. Keeps everything until it is
// deleted: in the app, or by removing the files themselves (see pruneMissing()).
class HistoryStore
{
public:
    explicit HistoryStore(QString indexPath);

    bool load();
    bool save() const;

    const QList<HistoryEntry>& entries() const { return m_entries; }
    const HistoryEntry* find(const QString& id) const;

    // A new entry goes first (newest); one with a known id is replaced where it is.
    void add(const HistoryEntry& entry);
    bool remove(const QString& id);
    // Records `path` as the entry's edited file for its extension, replacing any other.
    bool setEdited(const QString& id, const QString& path);
    // Forgets an export path wherever it is listed. True if any entry referenced it.
    bool removeExport(const QString& path);
    bool setThumb(const QString& id, const QString& path);
    // Default edits are stored as "none".
    bool setEdits(const QString& id, const EditState& edits);
    bool setSheet(const QString& id, const SheetState& sheet);
    // Forgets entries whose master file is gone and export paths that no longer exist. True if
    // anything changed; the dropped entries (e.g. to delete their thumbnails) go to `removed`.
    bool pruneMissing(QList<HistoryEntry>* removed = nullptr);

private:
    HistoryEntry* findMutable(const QString& id);

    QString m_indexPath;
    QList<HistoryEntry> m_entries;
};

} // namespace qr
