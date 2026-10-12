#pragma once

#include <QString>

namespace qr::theme
{

// Fusion style plus our own palette and stylesheet. `mode` is "system" (follows the
// desktop's light/dark preference, dark when it doesn't say), "dark" or "light".
// Safe to call again whenever the setting changes.
void apply(const QString& mode);

} // namespace qr::theme
