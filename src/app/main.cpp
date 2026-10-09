#include "BuildInfo.h"
#include "app/App.h"
#include "app/Icons.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QLocalServer>
#include <QLocalSocket>
#include <QMessageBox>
#include <spdlog/spdlog.h>
#include <unistd.h>

namespace
{
// Inside the AppImage we run through the bundled loader with bundled library paths.
// Once Qt has initialised (plugin paths cached, libraries resolved), put the host's
// values back so programs we spawn (xdg-open, the system ffmpeg) run normally.
void restoreHostEnvironment()
{
    if(qEnvironmentVariableIsEmpty("QUICKRECORD_APPDIR"))
        return;
    for(const char* name : {"LD_LIBRARY_PATH", "QT_PLUGIN_PATH"})
    {
        const QByteArray orig = qgetenv(QByteArray("QUICKRECORD_ORIG_") + name);
        if(orig.isEmpty())
            qunsetenv(name);
        else
            qputenv(name, orig);
    }
    qunsetenv("QML2_IMPORT_PATH");
}
} // namespace

int main(int argc, char** argv)
{
    // X11 only for now: make Qt use xcb even if a Wayland socket happens to exist.
    if(qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM"))
        qputenv("QT_QPA_PLATFORM", "xcb");

    // Everything here paints with QPainter. Without this, Qt Multimedia converts each
    // video frame through an OpenGL context, which on machines where GL ends up in
    // software (llvmpipe — e.g. NVIDIA under nixGLIntel) costs a full CPU core.
    QCoreApplication::setAttribute(Qt::AA_ForceRasterWidgets);

    QApplication app(argc, argv);
    QApplication::setApplicationName("quickrecord");
    QApplication::setApplicationDisplayName("QuickRecord");
    QApplication::setApplicationVersion(QUICKRECORD_VERSION);
    QApplication::setDesktopFileName("quickrecord");
    QApplication::setWindowIcon(qr::icons::app());
    QApplication::setQuitOnLastWindowClosed(false);
    restoreHostEnvironment();

    QCommandLineParser parser;
    parser.setApplicationDescription("Shutter-style screen recorder: region → GIF/MP4/PNG.");
    parser.addHelpOption();
    parser.addVersionOption();
    const QCommandLineOption toggle("toggle", "Start selecting a region, or stop the current recording.");
    const QCommandLineOption last("last", "Record the last used area again (or stop the current recording).");
    const QCommandLineOption screenshot("screenshot", "Pick an area for a screenshot.");
    const QCommandLineOption history("history", "Open the history window.");
    const QCommandLineOption settings("settings", "Open the settings dialog.");
    const QCommandLineOption quit("quit", "Quit the running instance.");
    parser.addOptions({toggle, last, screenshot, history, settings, quit});
    parser.process(app);

    QString command;
    if(parser.isSet(toggle))
        command = "toggle";
    else if(parser.isSet(last))
        command = "last";
    else if(parser.isSet(screenshot))
        command = "screenshot";
    else if(parser.isSet(history))
        command = "history";
    else if(parser.isSet(settings))
        command = "settings";
    else if(parser.isSet(quit))
        command = "quit";

    // Single instance: forward the command to a running daemon (default: show history).
    // Absolute path: a relative name lands in $TMPDIR, which differs between sessions
    // (e.g. every `nix develop` shell gets its own).
    QString runtimeDir = qEnvironmentVariable("XDG_RUNTIME_DIR");
    if(runtimeDir.isEmpty())
        runtimeDir = "/tmp";
    const QString serverName = QStringLiteral("%1/quickrecord-%2.sock").arg(runtimeDir).arg(getuid());
    {
        QLocalSocket probe;
        probe.connectToServer(serverName);
        if(probe.waitForConnected(300))
        {
            probe.write((command.isEmpty() ? QStringLiteral("history") : command).toUtf8() + "\n");
            probe.waitForBytesWritten(1000);
            probe.disconnectFromServer();
            return 0;
        }
    }
    if(command == "quit")
        return 0;

    if(QGuiApplication::platformName() != "xcb" || qEnvironmentVariable("XDG_SESSION_TYPE") == "wayland")
    {
        QMessageBox::critical(nullptr, "QuickRecord",
                              "QuickRecord currently supports X11 sessions only.\n"
                              "Log into an X11 session (e.g. Cinnamon on Xorg) to record.");
        return 1;
    }

    spdlog::info("QuickRecord {} starting", QUICKRECORD_VERSION);
    qr::App quickrecord;

    QLocalServer::removeServer(serverName);
    QLocalServer server;
    if(!server.listen(serverName))
        spdlog::warn("single-instance server failed: {}", server.errorString().toStdString());
    QObject::connect(&server, &QLocalServer::newConnection, &server, [&] {
        while(QLocalSocket* sock = server.nextPendingConnection())
        {
            QObject::connect(sock, &QLocalSocket::readyRead, sock, [sock, &quickrecord] {
                while(sock->canReadLine())
                    quickrecord.handleCommand(QString::fromUtf8(sock->readLine()).trimmed());
            });
            QObject::connect(sock, &QLocalSocket::disconnected, sock, &QObject::deleteLater);
        }
    });

    if(!command.isEmpty())
        quickrecord.handleCommand(command);
    return app.exec();
}
