#include "BuildInfo.h"
#include "app/App.h"
#include "app/Icons.h"
#include "app/Platform.h"
#include "app/Settings.h"
#include "app/Theme.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QLocalServer>
#include <QLocalSocket>
#include <QMessageBox>
#include <QPointer>
#include <cstdio>
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
#ifndef Q_OS_MACOS
    // X11 only for now: make Qt use xcb even if a Wayland socket happens to exist.
    if(qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM"))
        qputenv("QT_QPA_PLATFORM", "xcb");
#endif

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
    const QCommandLineOption edit("edit", "Open a recording or screenshot (by its id, e.g. qs-20261018-110900) in the editor.",
                                  "id");
    // For scripts: no UI, and each waits for the running instance's answer ("ok …" on stdout).
    const QCommandLineOption recordRect("record-rect",
                                        "Record this area (logical pixels, e.g. 100,80,1280,720) without the record "
                                        "bar; returns once capturing.",
                                        "x,y,w,h");
    const QCommandLineOption keys("keys", "With --record-rect: also record keys and clicks.");
    const QCommandLineOption stop("stop", "Stop the recording; prints \"ok <id>\\t<master.mp4>\" once it is written.");
    const QCommandLineOption exportId("export", "Export recording <id> to --out as MP4, keys and clicks burned in.", "id");
    const QCommandLineOption out("out", "With --export: the MP4 to write.", "path");
    const QCommandLineOption keysCorner("keys-corner",
                                        "With --export: where the keys go: top-left, top-center, top-right, "
                                        "bottom-left, bottom-center or bottom-right (default).",
                                        "corner", "bottom-right");
    parser.addOptions(
        {toggle, last, screenshot, history, settings, quit, edit, recordRect, keys, stop, exportId, out, keysCorner});
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
    else if(parser.isSet(edit))
        command = "edit " + parser.value(edit);
    // Scripted ones: answered by the running instance (see App::handleCommand).
    bool scripted = true;
    if(parser.isSet(recordRect))
        command = "record-rect\t" + parser.value(recordRect) + (parser.isSet(keys) ? "\tkeys" : "");
    else if(parser.isSet(stop))
        command = "stop";
    else if(parser.isSet(exportId))
        command = QStringList{"export", parser.value(exportId), parser.value(out), parser.value(keysCorner)}.join('\t');
    else
        scripted = false;

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
            if(scripted)
            {
                // One line back; an export can take a while.
                while(!probe.canReadLine() && probe.waitForReadyRead(10 * 60 * 1000))
                {
                }
                const QString answer = QString::fromUtf8(probe.readLine()).trimmed();
                fprintf(answer.startsWith("ok") ? stdout : stderr, "%s\n",
                        qPrintable(answer.isEmpty() ? QStringLiteral("error no answer") : answer));
                return answer.startsWith("ok") ? 0 : 1;
            }
            probe.disconnectFromServer();
            return 0;
        }
    }
    if(command == "quit")
        return 0;
    if(scripted)
    {
        fprintf(stderr, "error QuickRecord isn't running: start it first\n");
        return 2;
    }

    if(const QString reason = qr::platform::unsupportedReason(); !reason.isEmpty())
    {
        QMessageBox::critical(nullptr, "QuickRecord", reason);
        return 1;
    }
    qr::platform::init();

    spdlog::info("QuickRecord {} starting", QUICKRECORD_VERSION);
    const qr::Settings prefs = qr::Settings::load();
    qr::theme::apply(prefs.theme);
    qr::theme::setTextScale(prefs.textScale);
    qr::App quickrecord;

    QLocalServer::removeServer(serverName);
    QLocalServer server;
    if(!server.listen(serverName))
        spdlog::warn("single-instance server failed: {}", server.errorString().toStdString());
    QObject::connect(&server, &QLocalServer::newConnection, &server, [&] {
        while(QLocalSocket* sock = server.nextPendingConnection())
        {
            QObject::connect(sock, &QLocalSocket::readyRead, sock, [sock, &quickrecord] {
                QStringList commands;
                while(sock->canReadLine())
                    commands << QString::fromUtf8(sock->readLine()).trimmed();
                // Don't touch the socket after this except through `reply`: a command may process
                // events (quitting does on macOS), which deletes it once the client has disconnected.
                const QPointer<QLocalSocket> guarded(sock);
                const auto reply = [guarded](const QString& line) {
                    if(guarded)
                    {
                        guarded->write(line.toUtf8() + "\n");
                        guarded->flush();
                    }
                };
                for(const QString& c : commands)
                    quickrecord.handleCommand(c, reply);
            });
            QObject::connect(sock, &QLocalSocket::disconnected, sock, &QObject::deleteLater);
        }
    });

    if(!command.isEmpty())
        quickrecord.handleCommand(command);
    return app.exec();
}
