#include "devicecommands.h"

#include <algorithm>

#include <QFile>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QMutex>
#include <QRegularExpression>
#include <QSet>
#include <QThread>
#include <QUuid>

#include "../adb/adbexecutor.h"
#include "../adb/adbquote.h"
#include "../core/activitylog.h"
#include "../core/farmlog.h"
#include "../core/farmsettings.h"
#include "../core/taskexecutor.h"
#include "deviceregistry.h"
#include "deviceservice.h"

namespace farm {

namespace {
QMutex g_recordingMutex;
QSet<QString> g_recording;

int defaultConcurrency()
{
    return std::clamp(FarmSettings::instance().adbConcurrency(), 1, 32);
}

QString firstLine(const QString &s)
{
    const QString t = s.trimmed();
    const int nl = t.indexOf(QLatin1Char('\n'));
    return nl < 0 ? t : t.left(nl);
}
} // namespace

QString DeviceCommands::fileSafeId(const QString &id)
{
    QString s = id;
    s.replace(QLatin1Char(':'), QLatin1Char('_'));
    s.replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9_.\\-]")), QStringLiteral("_"));
    return s;
}

QString DeviceCommands::timestampForFile()
{
    return QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd_HH-mm-ss"));
}

BatchJob *DeviceCommands::make(const QString &name, const QStringList &ids, BatchJob::ItemFn fn, int concurrency, bool destructive, const QString &kind)
{
    auto *job = new BatchJob(name, ids, std::move(fn), concurrency > 0 ? concurrency : defaultConcurrency());
    job->setDestructive(destructive);
    if (!kind.isEmpty()) {
        job->setKind(kind);
    }
    JobManager::instance().startJob(job);
    return job;
}

BatchJob *DeviceCommands::simpleShell(const QString &name, const QStringList &ids, const QString &script, int timeoutMs, bool destructive)
{
    return make(name, ids, [script, timeoutMs](const QString &id, CancellationToken token, BatchJob::DoneFn done) {
        AdbCommand c;
        c.serial = id;
        c.args << QStringLiteral("shell") << script;
        c.timeoutMs = timeoutMs;
        c.label = QStringLiteral("shell");
        AdbExecutor::instance().run(c, nullptr, [done](const AdbResult &r) {
            const QString out = r.combined().trimmed();
            const bool ok = r.ok && !out.contains(QLatin1String("Exception"), Qt::CaseSensitive) && !out.startsWith(QLatin1String("Error"));
            done(ok, ok ? out : (out.isEmpty() ? r.error : out));
        }, token);
    }, 0, destructive);
}

// ---------------------------------------------------------------- screenshots

void DeviceCommands::captureImage(const QString &id, QObject *context, std::function<void(QImage, QString)> done, int timeoutMs)
{
    AdbCommand c;
    c.serial = id;
    c.args << QStringLiteral("exec-out") << QStringLiteral("screencap") << QStringLiteral("-p");
    c.timeoutMs = timeoutMs;
    c.binaryOutput = true;
    c.label = QStringLiteral("screencap");
    AdbExecutor::instance().run(c, context, [done](const AdbResult &r) {
        if (!r.ok) {
            done(QImage(), r.error);
            return;
        }
        QImage img = QImage::fromData(r.rawStdOut, "PNG");
        if (img.isNull()) {
            done(QImage(), QStringLiteral("screencap returned no image (%1 bytes)").arg(r.rawStdOut.size()));
            return;
        }
        done(img, QString());
    });
}

BatchJob *DeviceCommands::screenshot(const QStringList &ids, const QString &directory)
{
    QDir().mkpath(directory);
    const QString stamp = timestampForFile();
    return make(QObject::tr("Screenshot"), ids, [directory, stamp](const QString &id, CancellationToken token, BatchJob::DoneFn done) {
        AdbCommand c;
        c.serial = id;
        c.args << QStringLiteral("exec-out") << QStringLiteral("screencap") << QStringLiteral("-p");
        c.timeoutMs = 20000;
        c.binaryOutput = true;
        c.label = QStringLiteral("screencap");
        AdbExecutor::instance().run(c, nullptr, [done, directory, stamp, id](const AdbResult &r) {
            if (!r.ok) {
                done(false, r.error);
                return;
            }
            const QByteArray png = r.rawStdOut;
            // Encoding/writing happens on the media lane, never on the GUI thread.
            TaskExecutor::instance().run(QStringLiteral("media"), [done, directory, stamp, id, png]() {
                const QString path = QStringLiteral("%1/%2_%3.png").arg(directory, stamp, fileSafeId(id));
                QFile f(path);
                if (png.size() < 100 || !f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
                    done(false, QStringLiteral("cannot write %1").arg(path));
                    return;
                }
                f.write(png);
                f.close();
                done(true, path);
            });
        }, token);
    }, 4, false, QStringLiteral("screenshot"));
}

// ---------------------------------------------------------------- recording

bool DeviceCommands::isRecording(const QString &id)
{
    QMutexLocker lock(&g_recordingMutex);
    return g_recording.contains(id);
}

BatchJob *DeviceCommands::startRecording(const QStringList &ids, const QString &directory, int maxSeconds)
{
    QDir().mkpath(directory);
    const int seconds = std::clamp(maxSeconds, 1, 180);    // screenrecord's own hard limit is 180 s
    const QString stamp = timestampForFile();
    return make(QObject::tr("Screen recording"), ids, [directory, seconds, stamp](const QString &id, CancellationToken token, BatchJob::DoneFn done) {
        {
            QMutexLocker lock(&g_recordingMutex);
            if (g_recording.contains(id)) {
                done(false, QStringLiteral("already recording"));
                return;
            }
            g_recording.insert(id);
        }
        const QString remote = QStringLiteral("/sdcard/farm_rec_%1.mp4").arg(stamp);
        AdbCommand rec;
        rec.serial = id;
        rec.args << QStringLiteral("shell") << QStringLiteral("screenrecord --time-limit %1 %2").arg(seconds).arg(remote);
        rec.timeoutMs = (seconds + 30) * 1000;
        rec.label = QStringLiteral("screenrecord");
        AdbExecutor::instance().run(rec, nullptr, [done, id, remote, directory, stamp](const AdbResult &r) {
            {
                QMutexLocker lock(&g_recordingMutex);
                g_recording.remove(id);
            }
            if (r.timedOut || (!r.ok && !r.cancelled && !r.stdErr.contains(QLatin1String("Interrupt")))) {
                // screenrecord returns non-zero when stopped with SIGINT; that is fine.
            }
            if (r.cancelled) {
                done(false, QStringLiteral("cancelled"));
                return;
            }
            const QString local = QStringLiteral("%1/%2_%3.mp4").arg(directory, stamp, fileSafeId(id));
            AdbCommand pull;
            pull.serial = id;
            pull.args << QStringLiteral("pull") << remote << QDir::toNativeSeparators(local);
            pull.timeoutMs = 120000;
            pull.label = QStringLiteral("pull");
            AdbExecutor::instance().run(pull, nullptr, [done, id, remote, local](const AdbResult &p) {
                AdbCommand rm;
                rm.serial = id;
                rm.args << QStringLiteral("shell") << QStringLiteral("rm -f %1").arg(adb::shellQuote(remote));
                rm.timeoutMs = 8000;
                AdbExecutor::instance().run(rm, nullptr, nullptr);
                if (!p.ok) {
                    done(false, QStringLiteral("pull failed: %1").arg(p.error));
                    return;
                }
                done(QFileInfo::exists(local), local);
            });
        }, token);
    }, 4, false, QStringLiteral("recording"));
}

void DeviceCommands::stopRecording(const QStringList &ids)
{
    for (const QString &id : ids) {
        AdbCommand c;
        c.serial = id;
        // SIGINT lets screenrecord finalise the container (a plain kill leaves a broken mp4).
        c.args << QStringLiteral("shell") << QStringLiteral("pkill -INT screenrecord || killall -INT screenrecord");
        c.timeoutMs = 8000;
        c.label = QStringLiteral("stop-record");
        AdbExecutor::instance().run(c, nullptr, nullptr);
    }
}

// ---------------------------------------------------------------- applications

BatchJob *DeviceCommands::installApk(const QStringList &ids, const QString &apkPath, bool reinstall, bool grantPermissions)
{
    const QString native = QDir::toNativeSeparators(QFileInfo(apkPath).absoluteFilePath());
    return make(QObject::tr("Install %1").arg(QFileInfo(apkPath).fileName()), ids, [native, reinstall, grantPermissions](const QString &id, CancellationToken token, BatchJob::DoneFn done) {
        AdbCommand c;
        c.serial = id;
        c.args << QStringLiteral("install");
        if (reinstall) {
            c.args << QStringLiteral("-r");
        }
        if (grantPermissions) {
            c.args << QStringLiteral("-g");
        }
        c.args << native;
        c.timeoutMs = 300000;
        c.label = QStringLiteral("install");
        AdbExecutor::instance().run(c, nullptr, [done](const AdbResult &r) {
            const QString out = r.combined();
            const bool ok = r.ok && out.contains(QLatin1String("Success"));
            done(ok, ok ? QStringLiteral("Success") : (r.error.isEmpty() ? firstLine(out) : r.error));
        }, token);
    }, std::min(4, defaultConcurrency()), false, QStringLiteral("install"));
}

BatchJob *DeviceCommands::uninstall(const QStringList &ids, const QString &package, bool keepData)
{
    return make(QObject::tr("Uninstall %1").arg(package), ids, [package, keepData](const QString &id, CancellationToken token, BatchJob::DoneFn done) {
        AdbCommand c;
        c.serial = id;
        c.args << QStringLiteral("uninstall");
        if (keepData) {
            c.args << QStringLiteral("-k");
        }
        c.args << package;
        c.timeoutMs = 60000;
        c.label = QStringLiteral("uninstall");
        AdbExecutor::instance().run(c, nullptr, [done](const AdbResult &r) {
            const QString out = r.combined();
            const bool ok = out.contains(QLatin1String("Success"));
            done(ok, ok ? QStringLiteral("Success") : firstLine(out.isEmpty() ? r.error : out));
        }, token);
    }, 0, true, QStringLiteral("uninstall"));
}

BatchJob *DeviceCommands::launchApp(const QStringList &ids, const QString &package)
{
    const QString script = QStringLiteral("monkey -p %1 -c android.intent.category.LAUNCHER 1 >/dev/null 2>&1 && echo launched || "
                                          "(am start -n $(cmd package resolve-activity --brief %1 | tail -1) >/dev/null 2>&1 && echo launched)")
                               .arg(adb::shellQuote(package));
    return make(QObject::tr("Launch %1").arg(package), ids, [script](const QString &id, CancellationToken token, BatchJob::DoneFn done) {
        AdbCommand c;
        c.serial = id;
        c.args << QStringLiteral("shell") << script;
        c.timeoutMs = 20000;
        c.label = QStringLiteral("launch");
        AdbExecutor::instance().run(c, nullptr, [done](const AdbResult &r) {
            const bool ok = r.ok && r.stdOut.contains(QLatin1String("launched"));
            done(ok, ok ? QStringLiteral("launched") : firstLine(r.combined().isEmpty() ? r.error : r.combined()));
        }, token);
    });
}

BatchJob *DeviceCommands::forceStop(const QStringList &ids, const QString &package)
{
    return simpleShell(QObject::tr("Force stop %1").arg(package), ids, QStringLiteral("am force-stop %1 && echo stopped").arg(adb::shellQuote(package)), 15000);
}

BatchJob *DeviceCommands::clearData(const QStringList &ids, const QString &package)
{
    return simpleShell(QObject::tr("Clear data %1").arg(package), ids, QStringLiteral("pm clear %1").arg(adb::shellQuote(package)), 30000, true);
}

BatchJob *DeviceCommands::clearCache(const QStringList &ids, const QString &package)
{
    // No public API clears a single app's cache without root; deleting the
    // app's external cache dir is what is allowed. Report clearly when nothing applies.
    const QString script = QStringLiteral("rm -rf /sdcard/Android/data/%1/cache/* 2>/dev/null; pm trim-caches 1K >/dev/null 2>&1; echo 'external cache cleared'")
                               .arg(adb::shellQuote(package));
    return simpleShell(QObject::tr("Clear cache %1").arg(package), ids, script, 20000);
}

BatchJob *DeviceCommands::setPermission(const QStringList &ids, const QString &package, const QString &permission, bool grant)
{
    const QString script = QStringLiteral("pm %1 %2 %3 && echo ok").arg(grant ? QStringLiteral("grant") : QStringLiteral("revoke"), adb::shellQuote(package), adb::shellQuote(permission));
    return simpleShell(QObject::tr("%1 %2").arg(grant ? QStringLiteral("Grant") : QStringLiteral("Revoke"), permission), ids, script, 15000);
}

BatchJob *DeviceCommands::openAppInfo(const QStringList &ids, const QString &package)
{
    const QString script = QStringLiteral("am start -a android.settings.APPLICATION_DETAILS_SETTINGS -d package:%1 >/dev/null 2>&1 && echo ok").arg(adb::shellQuote(package));
    return simpleShell(QObject::tr("App info %1").arg(package), ids, script, 15000);
}

void DeviceCommands::listPackages(const QString &id, bool thirdPartyOnly, QObject *context, std::function<void(QList<adb::PackageInfo>, QString)> done)
{
    AdbCommand c;
    c.serial = id;
    c.args << QStringLiteral("shell") << QStringLiteral("pm list packages -f %1").arg(thirdPartyOnly ? QStringLiteral("-3") : QString());
    c.timeoutMs = 30000;
    c.label = QStringLiteral("pm-list");
    AdbExecutor::instance().run(c, context, [done](const AdbResult &r) {
        if (!r.ok) {
            done(QList<adb::PackageInfo>(), r.error);
            return;
        }
        QList<adb::PackageInfo> list = adb::parsePackages(r.stdOut);
        std::sort(list.begin(), list.end(), [](const adb::PackageInfo &a, const adb::PackageInfo &b) { return a.name < b.name; });
        done(list, QString());
    });
}

void DeviceCommands::packageDetails(const QString &id, const QString &package, QObject *context, std::function<void(PackageDetails, QString)> done)
{
    AdbCommand c;
    c.serial = id;
    c.args << QStringLiteral("shell") << QStringLiteral("dumpsys package %1").arg(adb::shellQuote(package));
    c.timeoutMs = 30000;
    c.label = QStringLiteral("dumpsys-package");
    AdbExecutor::instance().run(c, context, [done, package](const AdbResult &r) {
        PackageDetails d;
        d.package = package;
        if (!r.ok) {
            done(d, r.error);
            return;
        }
        static const QRegularExpression vn(QStringLiteral("versionName=(\\S+)"));
        static const QRegularExpression vc(QStringLiteral("versionCode=(\\d+)"));
        static const QRegularExpression path(QStringLiteral("codePath=(\\S+)"));
        static const QRegularExpression sdk(QStringLiteral("targetSdk=(\\d+)"));
        QRegularExpressionMatch m = vn.match(r.stdOut);
        if (m.hasMatch()) {
            d.versionName = m.captured(1);
        }
        m = vc.match(r.stdOut);
        if (m.hasMatch()) {
            d.versionCode = m.captured(1);
        }
        m = path.match(r.stdOut);
        if (m.hasMatch()) {
            d.apkPath = m.captured(1);
        }
        m = sdk.match(r.stdOut);
        if (m.hasMatch()) {
            d.targetSdk = m.captured(1);
        }
        // Permissions: "  android.permission.CAMERA: granted=true"
        static const QRegularExpression perm(QStringLiteral("^\\s*([\\w.]+): granted=(true|false)"), QRegularExpression::MultilineOption);
        QRegularExpressionMatchIterator it = perm.globalMatch(r.stdOut);
        while (it.hasNext()) {
            const QRegularExpressionMatch pm = it.next();
            if (pm.captured(2) == QLatin1String("true")) {
                if (!d.grantedPermissions.contains(pm.captured(1))) {
                    d.grantedPermissions << pm.captured(1);
                }
            } else if (!d.requestedPermissions.contains(pm.captured(1))) {
                d.requestedPermissions << pm.captured(1);
            }
        }
        static const QRegularExpression req(QStringLiteral("^\\s*([\\w.]+)$"), QRegularExpression::MultilineOption);
        const int reqStart = r.stdOut.indexOf(QLatin1String("requested permissions:"));
        if (reqStart >= 0) {
            const int end = r.stdOut.indexOf(QLatin1String("install permissions:"), reqStart);
            const QString block = r.stdOut.mid(reqStart, end > reqStart ? end - reqStart : 4000);
            QRegularExpressionMatchIterator ri = req.globalMatch(block);
            while (ri.hasNext()) {
                const QString p = ri.next().captured(1);
                if (p.contains(QLatin1Char('.')) && !d.grantedPermissions.contains(p) && !d.requestedPermissions.contains(p)) {
                    d.requestedPermissions << p;
                }
            }
        }
        done(d, QString());
    });
}

// ---------------------------------------------------------------- files

BatchJob *DeviceCommands::pushFile(const QStringList &ids, const QString &localPath, const QString &remoteDirectory)
{
    return pushFiles(ids, QStringList{ localPath }, remoteDirectory);
}

BatchJob *DeviceCommands::pushFiles(const QStringList &ids, const QStringList &localPaths, const QString &remoteDirectory)
{
    QString remoteDir = remoteDirectory.trimmed();
    if (remoteDir.isEmpty()) {
        remoteDir = QStringLiteral("/sdcard/Download");
    }
    if (!remoteDir.endsWith(QLatin1Char('/'))) {
        remoteDir += QLatin1Char('/');
    }
    QStringList natives;
    for (const QString &p : localPaths) {
        natives << QDir::toNativeSeparators(QFileInfo(p).absoluteFilePath());
    }
    const QString name = localPaths.size() == 1 ? QObject::tr("Upload %1").arg(QFileInfo(localPaths.first()).fileName()) : QObject::tr("Upload %1 files").arg(localPaths.size());
    return make(name, ids, [natives, remoteDir](const QString &id, CancellationToken token, BatchJob::DoneFn done) {
        // adb push accepts several local paths when the last argument is a directory.
        AdbCommand mk;
        mk.serial = id;
        mk.args << QStringLiteral("shell") << QStringLiteral("mkdir -p %1").arg(adb::shellQuote(remoteDir));
        mk.timeoutMs = 8000;
        AdbExecutor::instance().run(mk, nullptr, [done, id, natives, remoteDir, token](const AdbResult &) {
            AdbCommand c;
            c.serial = id;
            c.args << QStringLiteral("push");
            c.args << natives;
            c.args << remoteDir;
            c.timeoutMs = 600000;
            c.label = QStringLiteral("push");
            AdbExecutor::instance().run(c, nullptr, [done](const AdbResult &r) {
                const QString out = r.combined().trimmed();
                const bool ok = r.ok && !out.contains(QLatin1String("error"), Qt::CaseInsensitive);
                done(ok, ok ? firstLine(out) : (r.error.isEmpty() ? firstLine(out) : r.error));
            }, token);
        }, token);
    }, std::min(4, defaultConcurrency()), false, QStringLiteral("push"));
}

BatchJob *DeviceCommands::pullFile(const QStringList &ids, const QString &remotePath, const QString &localDirectory)
{
    QDir().mkpath(localDirectory);
    return make(QObject::tr("Download %1").arg(remotePath), ids, [remotePath, localDirectory](const QString &id, CancellationToken token, BatchJob::DoneFn done) {
        const QString dir = localDirectory + QLatin1Char('/') + fileSafeId(id);
        QDir().mkpath(dir);
        AdbCommand c;
        c.serial = id;
        c.args << QStringLiteral("pull") << remotePath << QDir::toNativeSeparators(dir);
        c.timeoutMs = 600000;
        c.label = QStringLiteral("pull");
        AdbExecutor::instance().run(c, nullptr, [done, dir](const AdbResult &r) {
            done(r.ok, r.ok ? dir : (r.error.isEmpty() ? firstLine(r.combined()) : r.error));
        }, token);
    }, std::min(4, defaultConcurrency()), false, QStringLiteral("pull"));
}

BatchJob *DeviceCommands::deleteRemote(const QStringList &ids, const QString &remotePath)
{
    if (remotePath.trimmed().isEmpty() || remotePath.trimmed() == QLatin1String("/") || remotePath.trimmed() == QLatin1String("/sdcard")) {
        return simpleShell(QObject::tr("Delete refused"), ids, QStringLiteral("echo 'refusing to delete a root directory'; false"), 5000, true);
    }
    return simpleShell(QObject::tr("Delete %1").arg(remotePath), ids, QStringLiteral("rm -rf %1 && echo deleted").arg(adb::shellQuote(remotePath)), 60000, true);
}

BatchJob *DeviceCommands::makeDirectory(const QStringList &ids, const QString &remotePath)
{
    return simpleShell(QObject::tr("Create folder %1").arg(remotePath), ids, QStringLiteral("mkdir -p %1 && echo created").arg(adb::shellQuote(remotePath)), 10000);
}

void DeviceCommands::listDirectory(const QString &id, const QString &remotePath, QObject *context, std::function<void(QList<adb::RemoteEntry>, QString)> done)
{
    AdbCommand c;
    c.serial = id;
    c.args << QStringLiteral("shell") << QStringLiteral("ls -la %1").arg(adb::shellQuote(remotePath.isEmpty() ? QStringLiteral("/sdcard") : remotePath));
    c.timeoutMs = 20000;
    c.label = QStringLiteral("ls");
    AdbExecutor::instance().run(c, context, [done](const AdbResult &r) {
        if (!r.ok) {
            done(QList<adb::RemoteEntry>(), r.error);
            return;
        }
        QList<adb::RemoteEntry> list = adb::parseLsLa(r.stdOut);
        std::sort(list.begin(), list.end(), [](const adb::RemoteEntry &a, const adb::RemoteEntry &b) {
            if (a.isDir != b.isDir) {
                return a.isDir;
            }
            return a.name.compare(b.name, Qt::CaseInsensitive) < 0;
        });
        done(list, QString());
    });
}

// ---------------------------------------------------------------- input

BatchJob *DeviceCommands::inputText(const QStringList &ids, const QString &text)
{
    bool ascii = true;
    for (const QChar ch : text) {
        if (ch.unicode() > 0x7E || ch.unicode() < 0x20) {
            ascii = false;
            break;
        }
    }
    return make(QObject::tr("Send text"), ids, [text, ascii](const QString &id, CancellationToken token, BatchJob::DoneFn done) {
        // Mirroring devices get the text through the scrcpy control channel, which
        // supports Unicode; others use `input text` (ASCII only).
        auto dev = DeviceService::instance().device(id);
        if (dev && DeviceService::instance().isMirroring(id)) {
            QString copy = text;
            dev->postTextInput(copy);
            done(true, QStringLiteral("sent via mirror channel"));
            return;
        }
        if (!ascii) {
            done(false, QStringLiteral("non-ASCII text needs an active mirror (or the helper app)"));
            return;
        }
        AdbCommand c;
        c.serial = id;
        c.args << QStringLiteral("shell") << QStringLiteral("input text %1").arg(adb::shellQuote(adb::inputTextEscape(text)));
        c.timeoutMs = 20000;
        c.label = QStringLiteral("input-text");
        AdbExecutor::instance().run(c, nullptr, [done](const AdbResult &r) { done(r.ok, r.ok ? QStringLiteral("sent") : r.error); }, token);
    }, 0, false, QStringLiteral("input"));
}

BatchJob *DeviceCommands::keyEvent(const QStringList &ids, const QString &keycode)
{
    return simpleShell(QObject::tr("Key %1").arg(keycode), ids, QStringLiteral("input keyevent %1 && echo ok").arg(adb::shellQuote(keycode)), 10000);
}

BatchJob *DeviceCommands::tap(const QStringList &ids, int x, int y)
{
    return simpleShell(QObject::tr("Tap %1,%2").arg(x).arg(y), ids, QStringLiteral("input tap %1 %2 && echo ok").arg(x).arg(y), 10000);
}

BatchJob *DeviceCommands::swipe(const QStringList &ids, int x1, int y1, int x2, int y2, int durationMs)
{
    return simpleShell(QObject::tr("Swipe"), ids, QStringLiteral("input swipe %1 %2 %3 %4 %5 && echo ok").arg(x1).arg(y1).arg(x2).arg(y2).arg(std::max(50, durationMs)), 15000);
}

BatchJob *DeviceCommands::setClipboardText(const QStringList &ids, const QString &text, bool paste)
{
    return make(QObject::tr("Set clipboard"), ids, [text, paste](const QString &id, CancellationToken, BatchJob::DoneFn done) {
        auto dev = DeviceService::instance().device(id);
        if (dev && DeviceService::instance().isMirroring(id)) {
            // The scrcpy channel pushes the PC clipboard; the caller has placed `text` there.
            dev->setDeviceClipboard(paste);
            done(true, QStringLiteral("clipboard set via mirror channel"));
            return;
        }
        done(false, QStringLiteral("clipboard needs an active mirror"));
    }, 0, false, QStringLiteral("clipboard"));
}

void DeviceCommands::getClipboardText(const QString &id, QObject *context, std::function<void(QString, QString)> done)
{
    auto dev = DeviceService::instance().device(id);
    if (dev && DeviceService::instance().isMirroring(id)) {
        // The core copies the device clipboard into the PC clipboard asynchronously.
        dev->requestDeviceClipboard();
        QMetaObject::invokeMethod(context, [done]() { done(QString(), QStringLiteral("requested — the device clipboard lands in the PC clipboard")); }, Qt::QueuedConnection);
        return;
    }
    QMetaObject::invokeMethod(context, [done]() { done(QString(), QStringLiteral("clipboard needs an active mirror")); }, Qt::QueuedConnection);
}

// ---------------------------------------------------------------- shell / adb

BatchJob *DeviceCommands::shell(const QStringList &ids, const QString &script, int timeoutMs, const QString &name)
{
    return make(name.isEmpty() ? QObject::tr("Shell: %1").arg(firstLine(script).left(40)) : name, ids, [script, timeoutMs](const QString &id, CancellationToken token, BatchJob::DoneFn done) {
        AdbCommand c;
        c.serial = id;
        c.args << QStringLiteral("shell") << script;
        c.timeoutMs = timeoutMs;
        c.label = QStringLiteral("shell");
        AdbExecutor::instance().run(c, nullptr, [done](const AdbResult &r) {
            const QString out = r.combined().trimmed();
            done(r.ok, out.isEmpty() ? r.error : out);
        }, token);
    }, 0, false, QStringLiteral("shell"));
}

BatchJob *DeviceCommands::adbCommand(const QStringList &ids, const QStringList &args, int timeoutMs, const QString &name)
{
    return make(name.isEmpty() ? QObject::tr("adb %1").arg(args.join(QLatin1Char(' ')).left(40)) : name, ids, [args, timeoutMs](const QString &id, CancellationToken token, BatchJob::DoneFn done) {
        AdbCommand c;
        c.serial = id;
        c.args = args;
        c.timeoutMs = timeoutMs;
        c.label = args.isEmpty() ? QStringLiteral("adb") : args.first();
        AdbExecutor::instance().run(c, nullptr, [done](const AdbResult &r) {
            const QString out = r.combined().trimmed();
            done(r.ok, out.isEmpty() ? r.error : out);
        }, token);
    }, 0, false, QStringLiteral("adb"));
}

void DeviceCommands::getProperties(const QString &id, QObject *context, std::function<void(QHash<QString, QString>, QString)> done)
{
    AdbCommand c;
    c.serial = id;
    c.args << QStringLiteral("shell") << QStringLiteral("getprop");
    c.timeoutMs = 15000;
    c.label = QStringLiteral("getprop");
    AdbExecutor::instance().run(c, context, [done](const AdbResult &r) {
        if (!r.ok) {
            done(QHash<QString, QString>(), r.error);
            return;
        }
        done(adb::parseGetProp(r.stdOut), QString());
    });
}

// ---------------------------------------------------------------- power

BatchJob *DeviceCommands::reboot(const QStringList &ids)
{
    return make(QObject::tr("Reboot"), ids, [](const QString &id, CancellationToken, BatchJob::DoneFn done) {
        DeviceService::instance().rebootDevice(id);
        done(true, QStringLiteral("reboot requested"));
    }, 0, true, QStringLiteral("reboot"));
}

BatchJob *DeviceCommands::wake(const QStringList &ids)
{
    return simpleShell(QObject::tr("Wake"), ids, QStringLiteral("input keyevent KEYCODE_WAKEUP && echo ok"), 8000);
}

BatchJob *DeviceCommands::screenOff(const QStringList &ids)
{
    return simpleShell(QObject::tr("Screen off"), ids, QStringLiteral("input keyevent KEYCODE_SLEEP && echo ok"), 8000);
}

// ---------------------------------------------------------------- settings profile

BatchJob *DeviceCommands::applySettings(const QStringList &ids, const QVariantMap &profile)
{
    QStringList cmds;
    QStringList checks;
    if (profile.contains(QStringLiteral("screenTimeoutMs"))) {
        cmds << QStringLiteral("settings put system screen_off_timeout %1").arg(profile.value(QStringLiteral("screenTimeoutMs")).toInt());
    }
    if (profile.contains(QStringLiteral("autoBrightness"))) {
        cmds << QStringLiteral("settings put system screen_brightness_mode %1").arg(profile.value(QStringLiteral("autoBrightness")).toBool() ? 1 : 0);
    }
    if (profile.contains(QStringLiteral("brightness"))) {
        cmds << QStringLiteral("settings put system screen_brightness_mode 0; settings put system screen_brightness %1").arg(std::clamp(profile.value(QStringLiteral("brightness")).toInt(), 0, 255));
    }
    if (profile.contains(QStringLiteral("volumeMedia"))) {
        cmds << QStringLiteral("media volume --stream 3 --set %1 >/dev/null 2>&1 || cmd media_session volume --stream 3 --set %1 >/dev/null 2>&1")
                    .arg(std::clamp(profile.value(QStringLiteral("volumeMedia")).toInt(), 0, 15));
    }
    if (profile.contains(QStringLiteral("animationScale"))) {
        const QString s = QString::number(profile.value(QStringLiteral("animationScale")).toDouble(), 'f', 2);
        cmds << QStringLiteral("settings put global window_animation_scale %1; settings put global transition_animation_scale %1; settings put global animator_duration_scale %1").arg(s);
    }
    if (profile.contains(QStringLiteral("wmSize"))) {
        const QString v = profile.value(QStringLiteral("wmSize")).toString();
        cmds << (v == QLatin1String("reset") ? QStringLiteral("wm size reset") : QStringLiteral("wm size %1").arg(adb::shellQuote(v)));
    }
    if (profile.contains(QStringLiteral("wmDensity"))) {
        const QString v = profile.value(QStringLiteral("wmDensity")).toString();
        cmds << (v == QLatin1String("reset") ? QStringLiteral("wm density reset") : QStringLiteral("wm density %1").arg(adb::shellQuote(v)));
    }
    if (profile.contains(QStringLiteral("orientation"))) {
        const QString o = profile.value(QStringLiteral("orientation")).toString();
        if (o == QLatin1String("auto")) {
            cmds << QStringLiteral("settings put system accelerometer_rotation 1");
        } else {
            cmds << QStringLiteral("settings put system accelerometer_rotation 0; settings put system user_rotation %1").arg(o == QLatin1String("landscape") ? 1 : 0);
        }
    }
    if (profile.contains(QStringLiteral("wifi"))) {
        cmds << QStringLiteral("svc wifi %1").arg(profile.value(QStringLiteral("wifi")).toBool() ? QStringLiteral("enable") : QStringLiteral("disable"));
    }
    if (profile.contains(QStringLiteral("bluetooth"))) {
        cmds << QStringLiteral("svc bluetooth %1").arg(profile.value(QStringLiteral("bluetooth")).toBool() ? QStringLiteral("enable") : QStringLiteral("disable"));
    }
    if (profile.contains(QStringLiteral("stayAwake"))) {
        cmds << QStringLiteral("svc power stayon %1").arg(profile.value(QStringLiteral("stayAwake")).toBool() ? QStringLiteral("true") : QStringLiteral("false"));
    }
    if (profile.contains(QStringLiteral("showTouches"))) {
        cmds << QStringLiteral("settings put system show_touches %1").arg(profile.value(QStringLiteral("showTouches")).toBool() ? 1 : 0);
    }
    if (cmds.isEmpty()) {
        return simpleShell(QObject::tr("Apply settings"), ids, QStringLiteral("echo nothing to apply"), 5000);
    }
    // Each setting reports its own failure so the operator sees which vendor
    // rejected what; the item fails if any single command failed.
    QStringList wrapped;
    for (int i = 0; i < cmds.size(); ++i) {
        wrapped << QStringLiteral("( %1 ) >/dev/null 2>&1 && echo 'ok %2' || echo 'FAIL %2: %3'").arg(cmds.at(i)).arg(i + 1).arg(firstLine(cmds.at(i)).left(60));
    }
    const QString script = wrapped.join(QStringLiteral("; "));
    return make(QObject::tr("Apply settings (%1)").arg(cmds.size()), ids, [script](const QString &id, CancellationToken token, BatchJob::DoneFn done) {
        AdbCommand c;
        c.serial = id;
        c.args << QStringLiteral("shell") << script;
        c.timeoutMs = 30000;
        c.label = QStringLiteral("settings");
        AdbExecutor::instance().run(c, nullptr, [done](const AdbResult &r) {
            const QString out = r.stdOut.trimmed();
            const bool ok = r.ok && !out.contains(QLatin1String("FAIL"));
            done(ok, out.isEmpty() ? r.error : out);
        }, token);
    }, 0, false, QStringLiteral("settings"));
}

} // namespace farm
