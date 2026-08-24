#ifndef FARM_DEVICES_DEVICECOMMANDS_H
#define FARM_DEVICES_DEVICECOMMANDS_H

#include <functional>

#include <QHash>
#include <QImage>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantMap>

#include "../adb/adbparsers.h"
#include "../core/batchjob.h"

namespace farm {

/**
 * High-level device operations. Every multi-device call returns a started
 * BatchJob (registered in JobManager) so the UI can show "17 / 80 completed",
 * cancel, and retry failed devices. Single-device queries take a completion
 * callback delivered on `context`'s thread.
 *
 * All device paths, package names and file names are quoted with adb::shellQuote;
 * arguments to adb itself are passed as separate argv entries — never as a
 * concatenated command line.
 */
class DeviceCommands
{
public:
    // ---- screenshots & recording ----
    /// PNG per device: <dir>/YYYY-MM-DD_HH-MM-SS_<device>.png (id with ':' -> '_').
    static BatchJob *screenshot(const QStringList &ids, const QString &directory);
    /// Grab one frame (adb screencap) as a QImage.
    static void captureImage(const QString &id, QObject *context, std::function<void(QImage, QString error)> done, int timeoutMs = 15000);
    /// Record up to maxSeconds on the device then pull the mp4 into <dir>.
    static BatchJob *startRecording(const QStringList &ids, const QString &directory, int maxSeconds);
    /// Stop an in-progress recording early (the file is finalised and pulled by the running job).
    static void stopRecording(const QStringList &ids);
    static bool isRecording(const QString &id);

    // ---- applications ----
    static BatchJob *installApk(const QStringList &ids, const QString &apkPath, bool reinstall = true, bool grantPermissions = false);
    static BatchJob *uninstall(const QStringList &ids, const QString &package, bool keepData = false);
    static BatchJob *launchApp(const QStringList &ids, const QString &package);
    static BatchJob *forceStop(const QStringList &ids, const QString &package);
    static BatchJob *clearData(const QStringList &ids, const QString &package);
    static BatchJob *clearCache(const QStringList &ids, const QString &package);
    static BatchJob *setPermission(const QStringList &ids, const QString &package, const QString &permission, bool grant);
    static BatchJob *openAppInfo(const QStringList &ids, const QString &package);
    static void listPackages(const QString &id, bool thirdPartyOnly, QObject *context,
                             std::function<void(QList<adb::PackageInfo>, QString error)> done);
    struct PackageDetails
    {
        QString package;
        QString versionName;
        QString versionCode;
        QString apkPath;
        QString targetSdk;
        QStringList grantedPermissions;
        QStringList requestedPermissions;
    };
    static void packageDetails(const QString &id, const QString &package, QObject *context, std::function<void(PackageDetails, QString error)> done);

    // ---- files ----
    static BatchJob *pushFile(const QStringList &ids, const QString &localPath, const QString &remoteDirectory);
    static BatchJob *pushFiles(const QStringList &ids, const QStringList &localPaths, const QString &remoteDirectory);
    static BatchJob *pullFile(const QStringList &ids, const QString &remotePath, const QString &localDirectory);
    static BatchJob *deleteRemote(const QStringList &ids, const QString &remotePath);
    static BatchJob *makeDirectory(const QStringList &ids, const QString &remotePath);
    static void listDirectory(const QString &id, const QString &remotePath, QObject *context,
                              std::function<void(QList<adb::RemoteEntry>, QString error)> done);

    // ---- input ----
    static BatchJob *inputText(const QStringList &ids, const QString &text);
    static BatchJob *keyEvent(const QStringList &ids, const QString &keycode);
    static BatchJob *tap(const QStringList &ids, int x, int y);
    static BatchJob *swipe(const QStringList &ids, int x1, int y1, int x2, int y2, int durationMs);
    /// Push the PC clipboard into the device clipboard (scrcpy channel when mirroring).
    static BatchJob *setClipboardText(const QStringList &ids, const QString &text, bool paste);
    static void getClipboardText(const QString &id, QObject *context, std::function<void(QString text, QString error)> done);

    // ---- shell / adb ----
    /// Runs `script` through `adb shell`; stdout ends up in Item::message.
    static BatchJob *shell(const QStringList &ids, const QString &script, int timeoutMs = 20000, const QString &name = QString());
    /// Raw adb arguments (without -s); stdout+stderr end up in Item::message.
    static BatchJob *adbCommand(const QStringList &ids, const QStringList &args, int timeoutMs = 30000, const QString &name = QString());
    static void getProperties(const QString &id, QObject *context, std::function<void(QHash<QString, QString>, QString error)> done);

    // ---- power / maintenance ----
    static BatchJob *reboot(const QStringList &ids);
    static BatchJob *wake(const QStringList &ids);
    static BatchJob *screenOff(const QStringList &ids);

    // ---- bulk device settings ----
    /// keys: screenTimeoutMs, brightness (0-255), autoBrightness (bool), volumeMedia (0-15),
    /// animationScale (double), wmSize ("1080x2220" or "reset"), wmDensity ("480" or "reset"),
    /// orientation ("auto"|"portrait"|"landscape"), wifi (bool), bluetooth (bool), stayAwake (bool),
    /// showTouches (bool)
    static BatchJob *applySettings(const QStringList &ids, const QVariantMap &profile);

    // ---- helpers ----
    static QString fileSafeId(const QString &id);
    static QString timestampForFile();

private:
    static BatchJob *make(const QString &name, const QStringList &ids, BatchJob::ItemFn fn, int concurrency = 0, bool destructive = false, const QString &kind = QString());
    static BatchJob *simpleShell(const QString &name, const QStringList &ids, const QString &script, int timeoutMs, bool destructive = false);
};

} // namespace farm

#endif // FARM_DEVICES_DEVICECOMMANDS_H
