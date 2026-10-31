#pragma once

#include "core/FfmpegArgs.h"
#include "core/HistoryStore.h"
#include "core/Sheet.h"

#include <QList>
#include <QObject>
#include <QString>
#include <functional>
#include <optional>

namespace qr
{

inline constexpr int kSpriteFrames = 8;
inline constexpr int kSpriteCellWidth = 240;

// Renders a one-row sprite sheet of kSpriteFrames thumbnails asynchronously.
// `done(ok)` runs on the GUI thread, only while `context` is alive.
void makeSprite(const QString& input, double durationSec, const QString& output, QObject* context,
                std::function<void(bool)> done);

// The cached frame that stands for a recording on a contact sheet.
QString sheetFramePath(const HistoryEntry& entry);
// The image of `entry` on a contact sheet: a screenshot's edited version or original, a recording's frame.
QString sheetImagePath(const HistoryEntry& entry);
// What a recording shows on a contact sheet: its edited MP4, else its other edited file, else the source.
QString sheetVideoPath(const HistoryEntry& entry);
// A video sheet ready for Exporter (output and encoder settings aside): its layers written to
// "<layers>-background.png" / "-labels.png", its clips placed on `tiles`. `entries` run alongside
// `tiles`; a null one, or a tile without image, is missing. No tiles in it means nothing to render.
SheetVideoParams sheetVideoParams(const SheetState& state, const QList<SheetTile>& tiles,
                                  const QList<const HistoryEntry*>& entries, const QString& name, const QDateTime& created,
                                  const QString& layers, QSize* size = nullptr);
// Writes the frames among `entries` that are missing or older than their video, one at a time, then
// runs `done` on the GUI thread, only while `context` is alive; at once if there are none. One that
// fails is left out: its tile shows as missing.
void makeSheetFrames(QList<HistoryEntry> entries, QObject* context, std::function<void()> done);

// Reads a video's duration, size and audio asynchronously. `done` gets nullopt if it can't be read,
// and runs on the GUI thread, only while `context` is alive.
void probeVideo(const QString& input, QObject* context, std::function<void(std::optional<VideoInfo>)> done);

} // namespace qr
