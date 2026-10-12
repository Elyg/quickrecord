#pragma once

#include <QIcon>

namespace qr::icons
{

QIcon app();
QIcon trayIdle();
QIcon trayRecording();
QIcon trayPaused();
QIcon trayBusy();

// Rounded colour chip with a grey outline, so white still shows on a light background.
QIcon swatch(const QColor& color);

} // namespace qr::icons
