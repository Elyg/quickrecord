#pragma once

#include <QString>

namespace qr::theme
{

// Fusion style plus our own palette and stylesheet. `mode` is "system" (follows the
// desktop's light/dark preference, dark when it doesn't say), "dark" or "light".
// Safe to call again whenever the setting changes.
void apply(const QString& mode);

// Scales the application font (and with it most controls) to `percent` of the platform
// default, so windows fit better on small or crowded screens.
void setTextScale(int percent);

} // namespace qr::theme
