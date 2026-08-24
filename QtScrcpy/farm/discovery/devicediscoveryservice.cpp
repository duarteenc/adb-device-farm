#include "devicediscoveryservice.h"

#include <algorithm>

#include <QHostAddress>
#include <QNetworkInterface>
#include <QProcess>

#include "../adb/adbexecutor.h"
#include "../core/activitylog.h"
#include "../core/farmlog.h"
#include "../core/farmsettings.h"
#include "../core/ipv4.h"
#include "../devices/deviceregistry.h"

namespace farm {

namespace {
const char *kComponent = "discovery";
}

DeviceDiscoveryService &DeviceDiscoveryService::instance()
{
    static DeviceDiscoveryService service;
    return service;
}

DeviceDiscoveryService::DeviceDiscoveryService(QObject *parent)
    : QObject(parent)
{
    qRegisterMetaType<QList<farm::adb::AdbDeviceInfo>>("QList<farm::adb::AdbDeviceInfo>");
    m_scanner = new NetworkScanner(this);
    connect(m_scanner, &NetworkScanner::hostFound, this, &DeviceDiscoveryService::onScannerHost);
    connect(m_scanner, &NetworkScanner::finished, this, &DeviceDiscoveryService::onScannerFinished);
    connect(m_scanner, &NetworkScanner::progress, this, [this](int done, int total) {
        m_scanDone = done;
        m_scanTotal = total;
        emit scanProgress(done, total);
    });
    m_quickTimer.setSingleShot(false);
    m_fullTimer.setSingleShot(true);
    connect(&m_quickTimer, &QTimer::timeout, this, &DeviceDiscoveryService::quickRefresh);
    connect(&m_fullTimer, &QTimer::timeout, this, &DeviceDiscoveryService::fullScan);
    connect(&FarmSettings::instance(), &FarmSettings::changed, this, [this](const QString &key) {
        if (key.startsWith(QLatin1String("discovery/")) || key.startsWith(QLatin1String("adb/"))) {
            applySettings();
        }
    });
}

void DeviceDiscoveryService::applySettings()
{
    const FarmSettings &s = FarmSettings::instance();
    m_connectConcurrency = std::clamp(s.connectConcurrency(), 1, 32);
    m_connectTimeoutMs = std::clamp(s.connectTimeoutMs(), 1000, 30000);
    if (m_running) {
        m_quickTimer.start(std::clamp(s.quickRefreshSeconds(), 1, 120) * 1000);
        if (!s.autoDiscovery()) {
            m_fullTimer.stop();
        } else if (!m_fullTimer.isActive() && !m_scanning) {
            m_fullTimer.start(1000);
        }
    }
}

void DeviceDiscoveryService::start()
{
    if (m_running) {
        return;
    }
    m_running = true;
    applySettings();
    // Startup: enumerate immediately, then kick the first LAN sweep right away.
    quickRefresh();
    if (FarmSettings::instance().autoDiscovery()) {
        m_fullTimer.start(300);
    }
    FarmLog::instance().info(QLatin1String(kComponent), QStringLiteral("started: subnet=%1 port=%2 quick=%3s full=%4s")
                                                              .arg(FarmSettings::instance().subnet())
                                                              .arg(FarmSettings::instance().adbPort())
                                                              .arg(FarmSettings::instance().quickRefreshSeconds())
                                                              .arg(FarmSettings::instance().fullScanSeconds()));
}

void DeviceDiscoveryService::stop()
{
    m_running = false;
    m_quickTimer.stop();
    m_fullTimer.stop();
    cancelScan();
}

QStringList DeviceDiscoveryService::localAddresses() const
{
    QStringList list;
    const QList<QHostAddress> addrs = QNetworkInterface::allAddresses();
    for (const QHostAddress &a : addrs) {
        if (a.protocol() == QAbstractSocket::IPv4Protocol) {
            list << a.toString();
        }
    }
    return list;
}

// ---------------------------------------------------------------- quick refresh (A + B)

void DeviceDiscoveryService::quickRefresh()
{
    if (m_quickInFlight) {
        return;
    }
    m_quickInFlight = true;
    AdbExecutor::instance().devices(this, [this](const AdbResult &r) {
        m_quickInFlight = false;
        if (!r.ok) {
            FarmLog::instance().warning(QLatin1String(kComponent), QStringLiteral("adb devices failed: %1").arg(r.error));
            emit statusMessage(tr("adb devices failed: %1").arg(r.error));
            return;
        }
        onQuickRefreshResult(adb::parseDevicesList(r.stdOut));
    });

    if (FarmSettings::instance().useMdns() && m_mdnsSupported) {
        AdbCommand c;
        c.args << QStringLiteral("mdns") << QStringLiteral("services");
        c.timeoutMs = 5000;
        c.label = QStringLiteral("mdns");
        AdbExecutor::instance().run(c, this, [this](const AdbResult &r) {
            if (!r.ok) {
                if (r.combined().contains(QLatin1String("unknown command"), Qt::CaseInsensitive)) {
                    m_mdnsSupported = false;
                }
                return;
            }
            const QList<adb::MdnsService> services = adb::parseMdnsServices(r.stdOut);
            for (const adb::MdnsService &s : services) {
                // _adb-tls-connect requires pairing; plain _adb._tcp can be connected directly.
                if (s.type.startsWith(QLatin1String("_adb._tcp")) && ipv4::isTcpEndpoint(s.address)) {
                    DeviceRegistry::instance().markDiscovered(ipv4::hostOf(s.address), ipv4::portOf(s.address));
                    DeviceRegistry::instance().update(s.address, [](DeviceRecord &d) { d.connectionType = ConnectionType::Mdns; });
                    if (FarmSettings::instance().autoConnect() && !m_onlineIds.contains(s.address)) {
                        enqueueConnect(s.address, false);    // never re-connect a device that is already online/mirroring
                    }
                }
            }
        });
    }
}

void DeviceDiscoveryService::onQuickRefreshResult(const QList<adb::AdbDeviceInfo> &devices)
{
    DeviceRegistry &registry = DeviceRegistry::instance();
    QSet<QString> nowOnline;
    for (const adb::AdbDeviceInfo &info : devices) {
        const QString id = registry.upsertFromAdb(info);
        if (info.isOnline()) {
            nowOnline.insert(id);
        }
    }
    // Appeared / disappeared transitions drive auto-connect and reconnect.
    for (const QString &id : nowOnline) {
        if (!m_onlineIds.contains(id)) {
            FarmLog::instance().info(QLatin1String(kComponent), QStringLiteral("device online"), id);
            emit deviceAppeared(id);
        }
    }
    for (const QString &id : m_onlineIds) {
        if (!nowOnline.contains(id)) {
            FarmLog::instance().info(QLatin1String(kComponent), QStringLiteral("device no longer listed by adb"), id);
            if (registry.contains(id)) {
                const DeviceRecord r = registry.get(id);
                bool listed = false;
                for (const adb::AdbDeviceInfo &info : devices) {
                    if (info.serial == id) {
                        listed = true;
                    }
                }
                if (!listed && r.state != DeviceState::Reconnecting) {
                    registry.setState(id, DeviceState::Offline);
                }
            }
            emit deviceDisappeared(id);
        }
    }
    m_onlineIds = nowOnline;
    m_lastSnapshot = devices;
    registry.autoNumber();    // newly seen devices get the lowest free farm numbers (numeric IP order)
    emit snapshotUpdated(devices);
}

// ---------------------------------------------------------------- full scan (C + D + E)

void DeviceDiscoveryService::readArpNeighbours()
{
    // `arp -a` is instantaneous; run it on the network lane and hand the list back.
    TaskExecutor::instance().run(QStringLiteral("network"), [this]() {
        QProcess p;
        p.start(QStringLiteral("arp"), QStringList() << QStringLiteral("-a"));
        QStringList hosts;
        if (p.waitForFinished(3000)) {
            hosts = adb::parseArpNeighbours(QString::fromLocal8Bit(p.readAllStandardOutput()));
        }
        QMetaObject::invokeMethod(this, [this, hosts]() { m_arpHosts = hosts; }, Qt::QueuedConnection);
    });
}

void DeviceDiscoveryService::fullScan()
{
    if (m_scanning || !m_running) {
        return;
    }
    const FarmSettings &s = FarmSettings::instance();
    ipv4::Range range;
    if (!ipv4::parseRange(s.subnet(), range)) {
        emit statusMessage(tr("Invalid subnet in Settings: %1").arg(s.subnet()));
        FarmLog::instance().error(QLatin1String(kComponent), QStringLiteral("invalid subnet %1").arg(s.subnet()));
        return;
    }
    if (s.useArp()) {
        readArpNeighbours();
    }
    NetworkScanner::Options options;
    options.range = range;
    options.port = static_cast<quint16>(std::clamp(s.adbPort(), 1, 65535));
    options.concurrency = std::clamp(s.scanConcurrency(), 4, 256);
    options.timeoutMs = std::clamp(s.scanTimeoutMs(), 200, 5000);
    options.excludeHosts = localAddresses();
    // Priority: known devices (C/F) then ARP neighbours (E).
    const QList<DeviceRecord> known = DeviceRegistry::instance().all();
    for (const DeviceRecord &d : known) {
        if (d.isTcp() && !d.lastIp.isEmpty()) {
            options.priorityHosts << d.lastIp;
        }
    }
    options.priorityHosts << m_arpHosts;

    m_scanToken = CancellationToken();
    m_scanning = true;
    m_manualScan = false;
    m_scanPort = options.port;
    m_scanNewlyConnected = 0;
    m_scanDone = 0;
    m_scanTotal = static_cast<int>(range.count());
    emit scanStarted(m_scanTotal);
    emit statusMessage(tr("Scanning %1…").arg(s.subnet()));
    FarmLog::instance().info(QLatin1String(kComponent), QStringLiteral("full scan %1 (%2 hosts, %3 parallel)").arg(s.subnet()).arg(range.count()).arg(options.concurrency));
    m_scanner->start(options, m_scanToken);
}

void DeviceDiscoveryService::cancelScan()
{
    if (m_scanning) {
        m_scanToken.cancel();
        m_scanner->cancel();
    }
}

void DeviceDiscoveryService::onScannerHost(const QString &host)
{
    const QString id = DeviceRegistry::instance().markDiscovered(host, m_scanPort);
    if (m_onlineIds.contains(id)) {
        return;
    }
    if (m_manualScan) {
        enqueueConnect(id, true);    // manual sweeps connect everything that answers
    } else if (FarmSettings::instance().autoConnect()) {
        enqueueConnect(id, false);
    }
}

void DeviceDiscoveryService::onScannerFinished(const QStringList &found, qint64 ms, bool cancelled)
{
    m_scanning = false;
    m_lastScanMs = ms;
    m_lastScanFound = static_cast<int>(found.size());
    m_lastFullScan = QDateTime::currentDateTime();
    DeviceRegistry::instance().autoNumber();
    FarmLog::instance().info(QLatin1String(kComponent), QStringLiteral("scan finished: %1 hosts on port in %2 ms%3").arg(found.size()).arg(ms).arg(cancelled ? QStringLiteral(" (cancelled)") : QString()));

    // Any known TCP device that did not answer at all is offline (unless mirroring
    // right now, which proves it is reachable).
    DeviceRegistry &registry = DeviceRegistry::instance();
    const QSet<QString> foundSet(found.begin(), found.end());
    if (!cancelled && !m_manualScan) {    // a manual sweep only probes its own range
        const QList<DeviceRecord> all = registry.all();
        for (const DeviceRecord &d : all) {
            if (d.isTcp() && !foundSet.contains(d.host()) && !m_onlineIds.contains(d.id)
                && (d.state == DeviceState::Discovered || d.state == DeviceState::Unknown)) {
                registry.setState(d.id, DeviceState::Offline);
            }
        }
    }

    // Adaptive interval: back off when nothing changes, snap back otherwise.
    const int base = std::clamp(FarmSettings::instance().fullScanSeconds(), 5, 3600);
    int factor = 1;
    if (FarmSettings::instance().adaptiveScan()) {
        if (m_scanNewlyConnected == 0) {
            m_idleScans = std::min(m_idleScans + 1, 3);
        } else {
            m_idleScans = 0;
        }
        factor = 1 << std::min(m_idleScans, 2);    // 1, 2, 4
    }
    if (m_running && FarmSettings::instance().autoDiscovery()) {
        m_fullTimer.start(base * factor * 1000);
    }
    // The connect fan-out may still be running; report when it drains (pumpConnects).
    if (m_connectQueue.isEmpty() && m_connecting.isEmpty()) {
        emit scanFinished(m_lastScanFound, m_scanNewlyConnected, ms);
        emit statusMessage(tr("Scan done: %1 hosts, %2 newly connected (%3 ms)").arg(m_lastScanFound).arg(m_scanNewlyConnected).arg(ms));
    } else {
        m_scanReportPending = true;
    }
}

// ---------------------------------------------------------------- adb connect fan-out

void DeviceDiscoveryService::enqueueConnect(const QString &endpoint, bool manual)
{
    if (m_connecting.contains(endpoint) || m_connectQueue.contains(endpoint)) {
        return;
    }
    if (!manual) {
        // Unauthorized / failing hosts are retried at most once per 2 minutes.
        const QDateTime last = m_recentConnectAttempt.value(endpoint);
        if (last.isValid() && last.secsTo(QDateTime::currentDateTime()) < 120) {
            const DeviceRecord r = DeviceRegistry::instance().get(endpoint);
            if (r.state == DeviceState::Unauthorized || r.state == DeviceState::Error) {
                return;
            }
        }
    } else {
        m_manualConnects.insert(endpoint);
    }
    m_connectQueue.append(endpoint);
    pumpConnects();
}

void DeviceDiscoveryService::pumpConnects()
{
    while (static_cast<int>(m_connecting.size()) < m_connectConcurrency && !m_connectQueue.isEmpty()) {
        const QString endpoint = m_connectQueue.takeFirst();
        m_connecting.insert(endpoint);
        m_recentConnectAttempt.insert(endpoint, QDateTime::currentDateTime());
        DeviceRegistry::instance().setState(endpoint, DeviceState::Connecting, tr("adb connect"));
        AdbExecutor::instance().connectEndpoint(endpoint, this, [this, endpoint](const AdbResult &r) {
            m_connecting.remove(endpoint);
            const bool manual = m_manualConnects.remove(endpoint);
            const bool ok = r.ok && adb::parseConnectSuccess(r.stdOut);
            DeviceRegistry &registry = DeviceRegistry::instance();
            if (ok) {
                if (!m_onlineIds.contains(endpoint)) {
                    ++m_scanNewlyConnected;
                }
                // The actual state (device/unauthorized/offline) comes from the next
                // `adb devices -l`; poll it right away rather than waiting for the timer.
                // (Discovered, not Connecting: Connecting is owned by DeviceService and
                // upsertFromAdb would never promote it to AdbOnline.)
                registry.setState(endpoint, DeviceState::Discovered, tr("verifying"));
                quickRefresh();
                ActivityLog::instance().info(ActivityEntry::Network, tr("Connected %1").arg(endpoint), endpoint);
            } else {
                const QString why = r.timedOut ? tr("timeout") : r.stdOut.trimmed().isEmpty() ? r.error : r.stdOut.trimmed();
                registry.setState(endpoint, DeviceState::Offline, why);
                FarmLog::instance().debug(QLatin1String(kComponent), QStringLiteral("connect failed: %1").arg(why), endpoint);
                if (manual) {
                    ActivityLog::instance().warning(ActivityEntry::Network, tr("Could not connect %1: %2").arg(endpoint, why), endpoint);
                }
            }
            pumpConnects();
        }, m_connectTimeoutMs);
    }
    if (m_scanReportPending && !m_scanning && m_connectQueue.isEmpty() && m_connecting.isEmpty()) {
        m_scanReportPending = false;
        emit scanFinished(m_lastScanFound, m_scanNewlyConnected, m_lastScanMs);
        emit statusMessage(tr("Scan done: %1 hosts, %2 newly connected (%3 ms)").arg(m_lastScanFound).arg(m_scanNewlyConnected).arg(m_lastScanMs));
    }
}

void DeviceDiscoveryService::connectEndpoint(const QString &endpointIn)
{
    QString endpoint = endpointIn.trimmed();
    if (endpoint.isEmpty()) {
        return;
    }
    if (!endpoint.contains(QLatin1Char(':'))) {
        endpoint += QStringLiteral(":%1").arg(FarmSettings::instance().adbPort());
    }
    if (!ipv4::isTcpEndpoint(endpoint)) {
        emit statusMessage(tr("Invalid endpoint: %1").arg(endpoint));
        return;
    }
    DeviceRegistry::instance().markDiscovered(ipv4::hostOf(endpoint), ipv4::portOf(endpoint));
    enqueueConnect(endpoint, true);
}

void DeviceDiscoveryService::connectRange(const QString &rangeText, quint16 port)
{
    ipv4::Range range;
    if (!ipv4::parseRange(rangeText, range)) {
        emit statusMessage(tr("Invalid IP range: %1").arg(rangeText));
        return;
    }
    if (m_scanning) {
        emit statusMessage(tr("A scan is already running"));
        return;
    }
    NetworkScanner::Options options;
    options.range = range;
    options.port = port;
    options.concurrency = std::clamp(FarmSettings::instance().scanConcurrency(), 4, 256);
    options.timeoutMs = std::clamp(FarmSettings::instance().scanTimeoutMs(), 200, 5000);
    options.excludeHosts = localAddresses();
    m_scanToken = CancellationToken();
    m_scanning = true;
    m_manualScan = true;    // onScannerHost connects everything that answers, on this port
    m_scanPort = port;
    m_scanNewlyConnected = 0;
    m_scanTotal = static_cast<int>(range.count());
    emit scanStarted(m_scanTotal);
    emit statusMessage(tr("Scanning %1 hosts…").arg(m_scanTotal));
    m_scanner->start(options, m_scanToken);
}

void DeviceDiscoveryService::disconnectEndpoint(const QString &endpoint)
{
    AdbExecutor::instance().disconnectEndpoint(endpoint, this, [this, endpoint](const AdbResult &) {
        DeviceRegistry::instance().setState(endpoint, DeviceState::Offline, tr("disconnected by operator"));
        quickRefresh();
    });
}

} // namespace farm
