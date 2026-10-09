#pragma once

#include <QObject>
#include <QString>
#include <functional>

namespace qr
{

inline constexpr int kSpriteFrames = 8;
inline constexpr int kSpriteCellWidth = 240;

// Renders a one-row sprite sheet of kSpriteFrames thumbnails asynchronously.
// `done(ok)` runs on the GUI thread, only while `context` is alive.
void makeSprite(const QString& input, double durationSec, const QString& output, QObject* context,
                std::function<void(bool)> done);

} // namespace qr
