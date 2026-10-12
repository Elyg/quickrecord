#pragma once

#include <QGuiApplication>

// Text rendering needs a QGuiApplication; the offscreen platform works headless.
// Inline, so every test file shares the one instance.
inline QGuiApplication& guiApp()
{
    static int argc = 1;
    static char name[] = "QuickRecordTests";
    static char* argv[] = {name, nullptr};
    if(qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM"))
        qputenv("QT_QPA_PLATFORM", "offscreen");
    static QGuiApplication app(argc, argv);
    return app;
}
