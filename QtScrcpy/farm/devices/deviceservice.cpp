#include "deviceservice.h"

#include <algorithm>

#include <QCoreApplication>
#include <QDateTime>
#include <QFileInfo>

#include "../adb/adbexecutor.h"
#include "../adb/adbparsers.h"
#include "../core/activitylog.h"
#include "../core/farmlog.h"
#include "../core/farmsettings.h"
#include "../core/ipv4.h"
#include "../discovery/devicediscoveryservice.h"
#include "connectionidallocator.h"
#include "deviceregistry.h"

namespace farm {

namespace {
const char *kComponent = "device";
const int kBackoffSeconds[] = { 1, 2, 5, 10, 30, 60 };
} // namespace

DeviceService &DeviceService::instance()
{
    static DeviceService service;
    return service;
}

DeviceService::DeviceService(QObject *parent)
    : QObject(parent)
{
    m_global = presetProfile(FarmSettings::instance().qualityPreset());
    const FarmSettings &s = FarmSettings::instance();
    if (s.value(QStringLiteral("mirror/maxSize")).isValid()) {
        m_global.maxSize = s.maxSize();
        m_global.bitRate = s.bitRate();
        m_global.maxFps = s.maxFps();
    }
}

void DeviceService::start()
{
    if (m_started) {
        return;
    }
    m_started = true;
    qsc::IDeviceManage &dm = qsc::IDeviceManage::getInstance();
    connect(&dm, &qsc::IDeviceManage::deviceConnected, this, &DeviceService::onCoreConnected);
    connect(&dm, &qsc::IDeviceManage::deviceDisconnected, this, &DeviceService::onCoreDisconnected);

    DeviceDiscoveryService &discovery = DeviceDiscoveryService::instance();
    connect(&discovery, &DeviceDiscoveryService::deviceAppeared, this, &DeviceService::onDeviceAppeared);
    connect(&discovery, &DeviceDiscoveryService::deviceDisappeared, this, &DeviceService::onDeviceDisappeared);

    connect(&FarmSettings::instance(), &FarmSettings::changed, this, [this](const QString &key) {
        if (key == QLatin1String("mirror/adaptiveQuality") || key == QLatin1String("mirror/maxConcurrentStarts")) {
            pump();
        }
    });
}

void DeviceService::shutdown()
{
    m_shuttingDown = true;
    for (QTimer *t : m_reconnectTimers) {
        t->stop();
    }
    for (QTimer *t : m_startTimers) {
        t->stop();
    }
    m_queue.clear();
    qsc::IDeviceManage::getInstance().disconnectAllDevice();
    for (const QString &id : m_mirroring) {
        ConnectionIdAllocator::instance().release(id);
    }
    m_mirroring.clear();
}

QPointer<qsc::IDevice> DeviceService::device(const QString &id) const
{
    return qsc::IDeviceManage::getInstance().getDevice(id);
}

bool DeviceService::isMirroring(const QString &id) const
{
    return m_mirroring.contains(id);
}

QString DeviceService::serverPath() const
{
    QString path = QString::fromLocal8Bit(qgetenv("QTSCRCPY_SERVER_PATH"));
    if (path.isEmpty() || !QFileInfo(path).isFile()) {
        path = QCoreApplication::applicationDirPath() + QStringLiteral("/scrcpy-server");
    }
    return path;
}

// ---------------------------------------------------------------- profiles

MirrorProfile DeviceService::presetProfile(const QString &name)
{
    MirrorProfile p;
    p.name = name;
    if (name == QLatin1String("performance")) {
        p.maxSize = 360;
        p.bitRate = 800000;
        p.maxFps = 15;
    } else if (name == QLatin1String("quality")) {
        p.maxSize = 1080;
        p.bitRate = 8000000;
        p.maxFps = 60;
    } else if (name == QLatin1String("focused")) {
        p.maxSize = 1080;
        p.bitRate = 10000000;
        p.maxFps = 60;
    } else {    // balanced
        p.name = QStringLiteral("balanced");
        p.maxSize = 720;
        p.bitRate = 3000000;
        p.maxFps = 30;
    }
    return p;
}

MirrorProfile DeviceService::globalProfile() const
{
    return m_global;
}

void DeviceService::setGlobalProfile(const MirrorProfile &profile, bool applyToRunning)
{
    m_global = profile;
    FarmSettings &s = FarmSettings::instance();
    s.setValue(QStringLiteral("mirror/preset"), profile.name);
    s.setValue(QStringLiteral("mirror/maxSize"), profile.maxSize);
    s.setValue(QStringLiteral("mirror/bitRate"), profile.bitRate);
    s.setValue(QStringLiteral("mirror/maxFps"), profile.maxFps);
    if (applyToRunning) {
        const QStringList ids = m_mirroring.values();
        for (const QString &id : ids) {
            restartMirror(id);
        }
    }
}

MirrorProfile DeviceService::profileFor(const QString &id) const
{
    MirrorProfile p = m_global;
    const DeviceRegistry &registry = DeviceRegistry::instance();
    const DeviceRecord r = registry.get(id);
    if (!r.group.isEmpty()) {
        const GroupInfo g = registry.group(r.group);
        if (g.settings.contains(QStringLiteral("preset")) && !g.settings.value(QStringLiteral("preset")).toString().isEmpty()) {
            p = presetProfile(g.settings.value(QStringLiteral("preset")).toString());
        }
        if (g.settings.value(QStringLiteral("maxSize")).toInt() > 0) {
            p.maxSize = g.settings.value(QStringLiteral("maxSize")).toInt();
        }
        if (g.settings.value(QStringLiteral("bitRate")).toInt() > 0) {
            p.bitRate = g.settings.value(QStringLiteral("bitRate")).toInt();
        }
        if (g.settings.value(QStringLiteral("fps")).toInt() > 0) {
            p.maxFps = g.settings.value(QStringLiteral("fps")).toInt();
        }
    }
    if (!r.preset.isEmpty()) {
        p = presetProfile(r.preset);
    }
    if (r.maxSize > 0) {
        p.maxSize = r.maxSize;
    }
    if (r.bitRate > 0) {
        p.bitRate = r.bitRate;
    }
    if (r.fps > 0) {
        p.maxFps = r.fps;
    }
    // Adaptive quality: many live mirrors -> lighter streams (operator opt-in).
    if (FarmSettings::instance().adaptiveQuality() && r.preset.isEmpty() && r.maxSize == 0) {
        const int live = static_cast<int>(m_mirroring.size() + m_starting.size() + m_queue.size());
        if (live > 50) {
            p.maxSize = std::min(p.maxSize, 360);
            p.maxFps = std::min(p.maxFps, 10);
            p.bitRate = std::min(p.bitRate, 600000);
        } else if (live > 20) {
            p.maxSize = std::min(p.maxSize, 480);
            p.maxFps = std::min(p.maxFps, 15);
            p.bitRate = std::min(p.bitRate, 1000000);
        } else if (live > 4) {
            p.maxSize = std::min(p.maxSize, 720);
            p.maxFps = std::min(p.maxFps, 30);
            p.bitRate = std::min(p.bitRate, 3000000);
        }
    }
    return p;
}

int DeviceService::maxConcurrentStarts() const
{
    return std::clamp(FarmSettings::instance().maxSimultaneousMirrorStarts(), 1, 16);
}

void DeviceService::applyAdaptiveQuality()
{
    // Profiles are resolved at start time; nothing to do for running sessions
    // (restarting them all would be worse than a slightly heavier stream).
}

// ---------------------------------------------------------------- mirror control

void DeviceService::startMirror(const QString &id)
{
    if (m_shuttingDown || id.isEmpty()) {
        return;
    }
    m_operatorStopped.remove(id);
    m_wanted.insert(id);
    if (m_mirroring.contains(id) || m_starting.contains(id) || m_queue.contains(id)) {
        return;
    }
    const DeviceRecord r = DeviceRegistry::instance().get(id);
    if (!DeviceRegistry::instance().contains(id)) {
        return;
    }
    if (r.state == DeviceState::Unauthorized) {
        DeviceRegistry::instance().setState(id, DeviceState::Unauthorized, tr("approve USB debugging on the phone"));
        return;
    }
    if (!r.isOnline() && r.state != DeviceState::Connecting) {
        // Not adb-online: (re)connect first; the appeared signal will start the mirror.
        if (r.isTcp()) {
            DeviceDiscoveryService::instance().connectEndpoint(id);
        }
        return;
    }
    m_queue.enqueue(id);
    emit queueChanged();
    pump();
}

void DeviceService::startMirror(const QStringList &ids)
{
    for (const QString &id : ids) {
        startMirror(id);
    }
}

void DeviceService::startMirrorAll()
{
    const QStringList ids = DeviceRegistry::instance().sorted(DeviceRegistry::SortKey::Number, true, DeviceRegistry::instance().onlineIds());
    for (const QString &id : ids) {
        startMirror(id);
    }
}

void DeviceService::pump()
{
    while (static_cast<int>(m_starting.size()) < maxConcurrentStarts() && !m_queue.isEmpty()) {
        const QString id = m_queue.dequeue();
        if (m_mirroring.contains(id) || m_starting.contains(id) || !m_wanted.contains(id)) {
            continue;
        }
        beginStart(id);
    }
    emit queueChanged();
}

void DeviceService::beginStart(const QString &id)
{
    m_starting.insert(id);
    m_startedAtMs.insert(id, QDateTime::currentMSecsSinceEpoch());
    DeviceRegistry::instance().setState(id, DeviceState::Connecting, tr("starting mirror"));

    // Watchdog: never let a hung device hold a start slot.
    QTimer *timer = m_startTimers.value(id, nullptr);
    if (!timer) {
        timer = new QTimer(this);
        timer->setSingleShot(true);
        m_startTimers.insert(id, timer);
        connect(timer, &QTimer::timeout, this, [this, id]() {
            if (m_starting.contains(id)) {
                FarmLog::instance().warning(QLatin1String(kComponent), QStringLiteral("mirror start timed out"), id);
                qsc::IDeviceManage::getInstance().disconnectDevice(id);
                finishStart(id, false, tr("mirror start timed out"));
            }
        });
    }
    timer->start(std::clamp(FarmSettings::instance().mirrorStartTimeoutMs(), 5000, 120000));

    const FarmSettings &s = FarmSettings::instance();
    if (s.normalizeResolution() && !m_normalized.contains(id)) {
        // Make every phone stream and accept control in the same coordinate space
        // (mixed native resolutions otherwise put broadcast taps in the wrong place).
        const QString script = QStringLiteral("wm size %1 ; wm density %2").arg(s.normalizedSize(), s.normalizedDensity());
        AdbExecutor::instance().shell(id, script, this, [this, id](const AdbResult &r) {
            if (!m_starting.contains(id)) {
                return;    // cancelled meanwhile
            }
            if (r.ok) {
                m_normalized.insert(id);
            } else {
                FarmLog::instance().warning(QLatin1String(kComponent), QStringLiteral("resolution normalize failed: %1").arg(r.error), id);
            }
            launchCore(id);
        }, 8000);
    } else {
        launchCore(id);
    }
}

void DeviceService::launchCore(const QString &id)
{
    if (!m_starting.contains(id)) {
        return;
    }
    const ConnectionIdAllocator::Lease lease = ConnectionIdAllocator::instance().acquire(id);
    if (!lease.valid()) {
        finishStart(id, false, tr("no free local port"));
        return;
    }
    const MirrorProfile profile = profileFor(id);
    m_activeProfiles.insert(id, profile);

    qsc::DeviceParams params;
    params.serial = id;
    params.serverLocalPath = serverPath();
    params.maxSize = static_cast<quint16>(std::clamp(profile.maxSize, 240, 2160));
    params.bitRate = static_cast<quint32>(std::clamp(profile.bitRate, 200000, 50000000));
    params.maxFps = static_cast<quint32>(std::clamp(profile.maxFps, 1, 120));
    params.useReverse = true;
    params.stayAwake = profile.stayAwake;
    params.renderExpiredFrames = false;    // latest-frame-wins
    params.scid = lease.scid;
    params.localPort = lease.localPort;
    params.recordPath = FarmSettings::instance().screenshotDirectory();
    params.logLevel = QStringLiteral("warn");

    if (!qsc::IDeviceManage::getInstance().connectDevice(params)) {
        ConnectionIdAllocator::instance().release(id);
        finishStart(id, false, tr("core refused connection (already connected?)"));
        return;
    }
    FarmLog::instance().debug(QLatin1String(kComponent),
                              QStringLiteral("core connect: scid=%1 port=%2 %3px %4fps %5bps").arg(lease.scid).arg(lease.localPort).arg(params.maxSize).arg(params.maxFps).arg(params.bitRate), id);
}

void DeviceService::onCoreConnected(bool success, const QString &serial, const QString &deviceName, const QSize &size)
{
    if (!success) {
        ConnectionIdAllocator::instance().release(serial);
        finishStart(serial, false, tr("scrcpy server failed to start"));
        return;
    }
    m_frameSizes.insert(serial, size);
    DeviceRegistry::instance().updateRuntime(serial, [&](DeviceRecord &r) {
        r.screenSize = size;
        if (!deviceName.isEmpty() && (r.model.isEmpty() || r.model.contains(QLatin1Char('-')))) {
            r.model = deviceName;
        }
    });
    finishStart(serial, true, QString());
}

void DeviceService::finishStart(const QString &id, bool success, const QString &reason)
{
    if (QTimer *t = m_startTimers.value(id, nullptr)) {
        t->stop();
    }
    const bool wasStarting = m_starting.remove(id);
    const qint64 started = m_startedAtMs.take(id);
    if (success) {
        m_mirroring.insert(id);
        m_reconnectAttempts.remove(id);
        if (started > 0) {
            m_lastConnectLatencyMs = QDateTime::currentMSecsSinceEpoch() - started;
            m_connectLatencyTotalMs += m_lastConnectLatencyMs;
            ++m_connectSamples;
        }
        DeviceRegistry::instance().updateRuntime(id, [](DeviceRecord &r) { r.reconnectAttempts = 0; });
        DeviceRegistry::instance().setState(id, DeviceState::Mirroring);
        ActivityLog::instance().info(ActivityEntry::Device, tr("Mirroring %1 (%2 ms)").arg(DeviceRegistry::instance().get(id).displayName()).arg(m_lastConnectLatencyMs), id);
        emit mirrorStarted(id, m_frameSizes.value(id));
    } else if (wasStarting) {
        DeviceRegistry::instance().setState(id, DeviceState::Error, reason);
        ActivityLog::instance().warning(ActivityEntry::Device, tr("Mirror failed on %1: %2").arg(id, reason), id);
        emit mirrorFailed(id, reason);
        if (m_wanted.contains(id) && !m_operatorStopped.contains(id) && FarmSettings::instance().autoReconnect()) {
            scheduleReconnect(id);
        }
    }
    pump();
}

void DeviceService::stopMirror(const QString &id, bool byOperator)
{
    if (byOperator) {
        m_wanted.remove(id);
        m_operatorStopped.insert(id);
        cancelReconnect(id);
    }
    m_queue.removeAll(id);
    if (m_starting.contains(id)) {
        if (QTimer *t = m_startTimers.value(id, nullptr)) {
            t->stop();
        }
        m_starting.remove(id);
        m_startedAtMs.remove(id);
    }
    if (qsc::IDeviceManage::getInstance().getDevice(id)) {
        qsc::IDeviceManage::getInstance().disconnectDevice(id);
    }
    if (m_mirroring.remove(id)) {
        ConnectionIdAllocator::instance().release(id);
        emit mirrorStopped(id);
    }
    const DeviceRecord r = DeviceRegistry::instance().get(id);
    if (r.state == DeviceState::Mirroring || r.state == DeviceState::Connecting) {
        DeviceRegistry::instance().setState(id, r.adbState == QLatin1String("device") ? DeviceState::AdbOnline : DeviceState::Offline);
    }
    pump();
}

void DeviceService::stopMirror(const QStringList &ids)
{
    for (const QString &id : ids) {
        stopMirror(id, true);
    }
}

void DeviceService::stopMirrorAll()
{
    const QStringList ids = m_mirroring.values() + m_starting.values() + QStringList(m_queue.cbegin(), m_queue.cend());
    for (const QString &id : ids) {
        stopMirror(id, true);
    }
}

void DeviceService::restartMirror(const QString &id)
{
    const bool wanted = m_wanted.contains(id) || m_mirroring.contains(id);
    stopMirror(id, false);
    if (wanted) {
        m_wanted.insert(id);
        m_operatorStopped.remove(id);
        // Give the core a moment to tear the old server down.
        QTimer::singleShot(400, this, [this, id]() { startMirror(id); });
    }
}

void DeviceService::onCoreDisconnected(const QString &serial)
{
    const bool wasLive = m_mirroring.remove(serial);
    const bool wasStarting = m_starting.contains(serial);
    if (wasStarting) {
        finishStart(serial, false, tr("stream ended during start"));
        return;
    }
    if (!wasLive) {
        return;
    }
    ConnectionIdAllocator::instance().release(serial);
    ++m_disconnectTotal;
    emit mirrorStopped(serial);
    if (m_shuttingDown) {
        return;
    }
    const DeviceRecord r = DeviceRegistry::instance().get(serial);
    if (m_wanted.contains(serial) && !m_operatorStopped.contains(serial) && FarmSettings::instance().autoReconnect()) {
        ActivityLog::instance().warning(ActivityEntry::Device, tr("%1 stream dropped — reconnecting").arg(r.displayName()), serial);
        scheduleReconnect(serial);
    } else if (r.state == DeviceState::Mirroring) {
        DeviceRegistry::instance().setState(serial, DeviceState::AdbOnline);
    }
    pump();
}

// ---------------------------------------------------------------- reconnect

void DeviceService::scheduleReconnect(const QString &id)
{
    if (m_shuttingDown || m_rebooting.contains(id)) {
        return;
    }
    const int attempt = m_reconnectAttempts.value(id, 0);
    const int maxAttempts = FarmSettings::instance().reconnectMaxAttempts();
    if (maxAttempts > 0 && attempt >= maxAttempts) {
        DeviceRegistry::instance().setState(id, DeviceState::Offline, tr("gave up after %1 attempts").arg(attempt));
        ActivityLog::instance().error(ActivityEntry::Device, tr("%1: reconnect gave up after %2 attempts").arg(id).arg(attempt), id);
        return;
    }
    const int idx = std::min(attempt, static_cast<int>(sizeof(kBackoffSeconds) / sizeof(kBackoffSeconds[0])) - 1);
    const int delayMs = kBackoffSeconds[idx] * 1000;
    m_reconnectAttempts.insert(id, attempt + 1);
    DeviceRegistry::instance().updateRuntime(id, [attempt](DeviceRecord &r) { r.reconnectAttempts = attempt + 1; });
    DeviceRegistry::instance().setState(id, DeviceState::Reconnecting, tr("retry %1 in %2 s").arg(attempt + 1).arg(delayMs / 1000));

    QTimer *timer = m_reconnectTimers.value(id, nullptr);
    if (!timer) {
        timer = new QTimer(this);
        timer->setSingleShot(true);
        m_reconnectTimers.insert(id, timer);
        connect(timer, &QTimer::timeout, this, [this, id]() { attemptReconnect(id); });
    }
    timer->start(delayMs);
    emit reconnectScheduled(id, attempt + 1, delayMs);
    FarmLog::instance().info(QLatin1String(kComponent), QStringLiteral("reconnect #%1 in %2 ms").arg(attempt + 1).arg(delayMs), id);
}

void DeviceService::attemptReconnect(const QString &id)
{
    if (m_shuttingDown || !m_wanted.contains(id) || m_operatorStopped.contains(id)) {
        return;
    }
    ++m_reconnectTotal;
    const DeviceRecord r = DeviceRegistry::instance().get(id);
    if (r.isTcp()) {
        AdbExecutor::instance().connectEndpoint(id, this, [this, id](const AdbResult &res) {
            const bool ok = res.ok && adb::parseConnectSuccess(res.stdOut);
            if (!ok) {
                DeviceRegistry::instance().setState(id, DeviceState::Reconnecting, tr("unreachable"));
                scheduleReconnect(id);
                return;
            }
            // Confirm adb sees it as "device" before starting the server.
            AdbExecutor::instance().shell(id, QStringLiteral("echo ok"), this, [this, id](const AdbResult &probe) {
                if (!probe.ok) {
                    scheduleReconnect(id);
                    return;
                }
                DeviceRegistry::instance().setState(id, DeviceState::AdbOnline);
                DeviceDiscoveryService::instance().quickRefresh();
                ActivityLog::instance().info(ActivityEntry::Device, tr("%1 reconnected").arg(id), id);
                if (!m_mirroring.contains(id) && !m_starting.contains(id) && !m_queue.contains(id)) {
                    m_queue.enqueue(id);
                    pump();
                }
            }, 5000);
        }, std::clamp(FarmSettings::instance().connectTimeoutMs(), 1000, 30000));
    } else {
        // USB: adb re-enumerates on its own; probe and start when it answers.
        AdbExecutor::instance().shell(id, QStringLiteral("echo ok"), this, [this, id](const AdbResult &probe) {
            if (!probe.ok) {
                scheduleReconnect(id);
                return;
            }
            DeviceRegistry::instance().setState(id, DeviceState::AdbOnline);
            if (!m_mirroring.contains(id) && !m_starting.contains(id) && !m_queue.contains(id)) {
                m_queue.enqueue(id);
                pump();
            }
        }, 5000);
    }
}

void DeviceService::cancelReconnect(const QString &id)
{
    if (QTimer *t = m_reconnectTimers.value(id, nullptr)) {
        t->stop();
    }
    m_reconnectAttempts.remove(id);
}

void DeviceService::reconnectDevice(const QString &id)
{
    m_operatorStopped.remove(id);
    m_wanted.insert(id);
    cancelReconnect(id);
    if (m_mirroring.contains(id)) {
        restartMirror(id);
    } else {
        attemptReconnect(id);
    }
}

// ---------------------------------------------------------------- discovery hooks

void DeviceService::onDeviceAppeared(const QString &id)
{
    if (m_shuttingDown) {
        return;
    }
    if (m_rebooting.remove(id)) {
        ActivityLog::instance().info(ActivityEntry::Device, tr("%1 is back after reboot").arg(id), id);
    }
    cancelReconnect(id);
    const DeviceRecord r = DeviceRegistry::instance().get(id);
    const bool shouldMirror = m_wanted.contains(id) || (FarmSettings::instance().autoMirror() && r.autoMirror && !m_operatorStopped.contains(id));
    if (shouldMirror) {
        startMirror(id);
    }
}

void DeviceService::onDeviceDisappeared(const QString &id)
{
    if (m_shuttingDown) {
        return;
    }
    // The core notices a dead stream by itself; but a device that silently drops
    // out of `adb devices` while we still think we're mirroring must be torn down
    // so the reconnect loop can take over.
    if (m_mirroring.contains(id) || m_starting.contains(id)) {
        FarmLog::instance().warning(QLatin1String(kComponent), QStringLiteral("device vanished from adb while mirroring; tearing down"), id);
        qsc::IDeviceManage::getInstance().disconnectDevice(id);
    } else if (m_wanted.contains(id) && !m_operatorStopped.contains(id) && FarmSettings::instance().autoReconnect()) {
        scheduleReconnect(id);
    }
}

// ---------------------------------------------------------------- adb-level

void DeviceService::connectDevice(const QString &id)
{
    m_operatorStopped.remove(id);
    m_wanted.insert(id);
    const DeviceRecord r = DeviceRegistry::instance().get(id);
    if (r.isOnline()) {
        startMirror(id);
    } else if (r.isTcp()) {
        DeviceDiscoveryService::instance().connectEndpoint(id);
    }
}

void DeviceService::disconnectDevice(const QString &id)
{
    stopMirror(id, true);
    if (ipv4::isTcpEndpoint(id)) {
        DeviceDiscoveryService::instance().disconnectEndpoint(id);
    }
}

void DeviceService::rebootDevice(const QString &id)
{
    m_rebooting.insert(id);
    const bool wanted = m_wanted.contains(id) || m_mirroring.contains(id);
    stopMirror(id, false);
    if (wanted) {
        m_wanted.insert(id);
    }
    DeviceRegistry::instance().setState(id, DeviceState::Reconnecting, tr("rebooting"));
    AdbCommand c;
    c.serial = id;
    c.args << QStringLiteral("reboot");
    c.timeoutMs = 10000;
    c.label = QStringLiteral("reboot");
    AdbExecutor::instance().run(c, this, [this, id](const AdbResult &) {
        ActivityLog::instance().info(ActivityEntry::Device, tr("Rebooting %1").arg(id), id);
        // Give the phone time to go down before we start probing; discovery's
        // quick refresh + reconnect loop bring it back.
        QTimer::singleShot(20000, this, [this, id]() {
            if (m_rebooting.contains(id)) {
                m_reconnectAttempts.insert(id, 2);    // start with a gentler 5 s cadence
                m_rebooting.remove(id);
                if (m_wanted.contains(id)) {
                    scheduleReconnect(id);
                }
            }
        });
    });
}

} // namespace farm
