#pragma once

#include "core/Release.h"

#include <QObject>
#include <QPointer>

#include <memory>
#include <optional>

class QNetworkAccessManager;
class QNetworkReply;
class QSaveFile;

namespace qr
{

// Checks GitHub's latest release, and installs it where this copy can replace itself:
//   AppImage           downloads the new AppImage over this one, then restarts it;
//   WindowsInstaller   runs the new setup silently, which quits this app, upgrades it and
//                      starts it again;
//   MacApp             downloads the .dmg; once this app has quit, a script swaps the app
//                      bundle for the dmg's and opens it;
//   Manual             (the portable Windows zip, an app that can't be replaced, builds from
//                      source) opens the release page instead.
// Only talks to the network when asked to.
class Updater : public QObject
{
    Q_OBJECT

public:
    enum class Mode
    {
        AppImage,
        WindowsInstaller,
        MacApp,
        Manual,
    };

    enum class Status
    {
        UpToDate,
        Available,
        DevBuild, // this build has no release version to compare (e.g. "dev")
        Failed,
    };

    struct Result
    {
        Status status = Status::Failed;
        QString latest;          // the latest release's version
        bool canInstall = false; // Available, and this copy can install it itself
        QString message;         // Failed: why
    };

    explicit Updater(QString currentVersion, QObject* parent = nullptr);
    ~Updater() override;

    QString currentVersion() const { return m_current; }
    Mode mode() const { return m_mode; }

    void check();
    // After an Available result with canInstall: downloads and installs it.
    void install();
    void openReleasePage() const;
    // After installed(): what's left to do. AppImage / MacApp: starts what puts the new version
    // in place once this process has exited, and returns true: quit now. WindowsInstaller:
    // starts the setup and returns false, as the setup quits us.
    bool finish();

signals:
    void checked(qr::Updater::Result result);
    void progress(int percent);
    void installed();
    void failed(QString message);

private:
    void downloadFinished();
    QString errorText(QNetworkReply* reply) const;

    QString m_current;
    Mode m_mode = Mode::Manual;
    QString m_appImage;  // AppImage mode: the file to replace
    QString m_appBundle; // MacApp mode: the QuickRecord.app to replace
    QNetworkAccessManager* m_network;
    std::optional<Release> m_release;
    std::optional<ReleaseAsset> m_asset;
    QPointer<QNetworkReply> m_reply;
    std::unique_ptr<QSaveFile> m_file;
    QString m_downloaded; // WindowsInstaller / MacApp: the downloaded setup or dmg
};

} // namespace qr
