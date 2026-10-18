#include "app/InputMonitor.h"

namespace qr
{

InputMonitor::InputMonitor(QObject* parent)
    : QObject(parent)
{
}

InputMonitor::~InputMonitor() = default;

bool InputMonitor::supported()
{
    return false;
}

bool InputMonitor::start(QString* error)
{
    *error = "Recording keys and clicks isn't available on X11 yet.";
    return false;
}

void InputMonitor::stop() {}

} // namespace qr
