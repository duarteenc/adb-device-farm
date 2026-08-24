#include "perfmonitor.h"

#include <algorithm>

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QThread>

#ifdef Q_OS_WIN
#include <windows.h>
#include <psapi.h>
#endif

#include "../adb/adbexecutor.h"
#include "../core/batchjob.h"
#include "../core/farmsettings.h"
#include "../core/taskexecutor.h"
#include "../devices/deviceregistry.h"
#include "../devices/deviceservice.h"
#include "../discovery/devicediscoveryservice.h"

namespace farm {

PerfMonitor &PerfMonitor::instance()
{
    static PerfMonitor monitor;
    return monitor;
}

PerfMonitor::PerfMonitor(QObject *parent)
    : QObject(parent)
{
    qRegisterMetaType<farm::PerfSnapshot>("farm::PerfSnapshot");
    m_cores = std::max(1, QThread::idealThreadCount());
    connect(&m_timer, &QTimer::timeout, this, &PerfMonitor::sample);
    connect(&m_lagTimer, &QTimer::timeout, this, &PerfMonitor::probeUiLag);
}

void PerfMonitor::start()
{
    m_intervalClock.start();
    m_lagClock.start();
    m_lastWallMs = 0;
    m_timer.start(std::clamp(FarmSettings::instance().perfSampleMs(), 250, 10000));
    m_lagTimer.start(100);
    connect(&FarmSettings::instance(), &FarmSettings::changed, this, [this](const QString &key) {
        if (key == QLatin1String("perf/sampleMs")) {
            m_timer.start(std::clamp(FarmSettings::instance().perfSampleMs(), 250, 10000));
        }
    });
}

void PerfMonitor::stop()
{
    m_timer.stop();
    m_lagTimer.stop();
}

void PerfMonitor::probeUiLag()
{
    // A 100 ms timer that fires late tells us the GUI thread was busy: the overshoot
    // is the stall the operator would have felt.
    const qint64 now = m_lagClock.elapsed();
    static qint64 expected = 0;
    if (expected > 0) {
        const int lag = static_cast<int>(std::max<qint64>(0, now - expected));
        m_lagMax = std::max(m_lagMax, lag);
        m_lagSum += lag;
        ++m_lagSamples;
    }
    expected = now + 100;
}

void PerfMonitor::sample()
{
    PerfSnapshot s;
    s.time = QDateTime::currentDateTime();
    const qint64 wallMs = m_intervalClock.elapsed();
    const double intervalSec = std::max<qint64>(1, wallMs - m_lastWallMs) / 1000.0;

#ifdef Q_OS_WIN
    FILETIME create, exit, kernel, user;
    if (GetProcessTimes(GetCurrentProcess(), &create, &exit, &kernel, &user)) {
        ULARGE_INTEGER k, u;
        k.LowPart = kernel.dwLowDateTime;
        k.HighPart = kernel.dwHighDateTime;
        u.LowPart = user.dwLowDateTime;
        u.HighPart = user.dwHighDateTime;
        const quint64 total = k.QuadPart + u.QuadPart;    // 100 ns units
        if (m_lastProcTime100ns > 0) {
            const double cpuSec = static_cast<double>(total - m_lastProcTime100ns) / 1e7;
            s.cpuPercentOneCore = cpuSec / intervalSec * 100.0;
            s.cpuPercent = s.cpuPercentOneCore / m_cores;
        }
        m_lastProcTime100ns = total;
    }
    PROCESS_MEMORY_COUNTERS_EX pmc;
    pmc.cb = sizeof(pmc);
    if (GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS *>(&pmc), sizeof(pmc))) {
        s.workingSetMb = static_cast<qint64>(pmc.WorkingSetSize / (1024 * 1024));
        s.privateMb = static_cast<qint64>(pmc.PrivateUsage / (1024 * 1024));
    }
    DWORD handles = 0;
    if (GetProcessHandleCount(GetCurrentProcess(), &handles)) {
        s.handles = static_cast<int>(handles);
    }
#endif
    s.threads = 0;    // filled by the UI page via QThread count if wanted

    // frames
    const qint64 decoded = m_decoded.exchange(0);
    const qint64 rendered = m_rendered.exchange(0);
    const qint64 dropped = m_dropped.exchange(0);
    const qint64 displayed = m_displayed.exchange(0);
    const qint64 handleUs = m_handleUs.exchange(0);
    const qint64 displayUs = m_displayUs.exchange(0);
    s.decodedFps = decoded / intervalSec;
    s.renderedFps = rendered / intervalSec;
    s.droppedFps = dropped / intervalSec;
    s.avgFrameHandleMs = rendered > 0 ? (handleUs / 1000.0) / rendered : -1;
    s.avgDisplayLatencyMs = displayed > 0 ? (displayUs / 1000.0) / displayed : -1;

    // adb
    const AdbExecutor::Metrics m = AdbExecutor::instance().metrics();
    const qint64 ops = m.total - m_lastAdbTotal;
    const qint64 dur = m.totalDurationMs - m_lastAdbDurationMs;
    s.adbOpsPerSec = ops / intervalSec;
    s.adbAvgMs = ops > 0 ? static_cast<double>(dur) / ops : -1;
    s.adbActive = m.active;
    s.adbQueued = m.queued;
    s.adbTotal = m.total;
    s.adbFailed = m.failed;
    m_lastAdbTotal = m.total;
    m_lastAdbDurationMs = m.totalDurationMs;

    // devices
    const DeviceRegistry &registry = DeviceRegistry::instance();
    s.devicesKnown = registry.count();
    s.devicesOnline = static_cast<int>(registry.onlineIds().size());
    const DeviceService &ds = DeviceService::instance();
    s.mirroring = ds.mirroringCount();
    s.connectQueue = ds.queuedCount();
    s.connectStarting = ds.startingCount();
    s.lastConnectLatencyMs = ds.lastConnectLatencyMs();
    s.avgConnectLatencyMs = ds.averageConnectLatencyMs();
    s.reconnects = ds.reconnectCount();
    s.disconnects = ds.disconnectCount();
    s.lastScanMs = DeviceDiscoveryService::instance().lastScanDurationMs();

    // jobs
    s.batchJobs = JobManager::instance().activeCount();
    s.automationJobs = TaskExecutor::instance().activeCount(QStringLiteral("automation"));
    s.automationQueue = TaskExecutor::instance().queuedCount(QStringLiteral("automation")) + JobManager::instance().queueDepth();

    // ui lag
    s.uiLagMaxMs = m_lagMax;
    s.uiLagAvgMs = m_lagSamples > 0 ? static_cast<double>(m_lagSum) / m_lagSamples : 0;
    m_lagMax = 0;
    m_lagSum = 0;
    m_lagSamples = 0;

    m_lastWallMs = wallMs;
    m_current = s;
    m_history.append(s);
    while (m_history.size() > m_historyLength) {
        m_history.removeFirst();
    }
    emit sampled(s);
}

namespace {
QJsonObject snapshotToJson(const PerfSnapshot &s)
{
    QJsonObject o;
    o[QStringLiteral("time")] = s.time.toString(Qt::ISODateWithMs);
    o[QStringLiteral("cpuPercent")] = s.cpuPercent;
    o[QStringLiteral("cpuPercentOneCore")] = s.cpuPercentOneCore;
    o[QStringLiteral("workingSetMb")] = static_cast<double>(s.workingSetMb);
    o[QStringLiteral("privateMb")] = static_cast<double>(s.privateMb);
    o[QStringLiteral("handles")] = s.handles;
    o[QStringLiteral("devicesKnown")] = s.devicesKnown;
    o[QStringLiteral("devicesOnline")] = s.devicesOnline;
    o[QStringLiteral("mirroring")] = s.mirroring;
    o[QStringLiteral("decodedFps")] = s.decodedFps;
    o[QStringLiteral("renderedFps")] = s.renderedFps;
    o[QStringLiteral("droppedFps")] = s.droppedFps;
    o[QStringLiteral("avgFrameHandleMs")] = s.avgFrameHandleMs;
    o[QStringLiteral("avgDisplayLatencyMs")] = s.avgDisplayLatencyMs;
    o[QStringLiteral("adbOpsPerSec")] = s.adbOpsPerSec;
    o[QStringLiteral("adbAvgMs")] = s.adbAvgMs;
    o[QStringLiteral("adbActive")] = s.adbActive;
    o[QStringLiteral("adbQueued")] = s.adbQueued;
    o[QStringLiteral("adbTotal")] = static_cast<double>(s.adbTotal);
    o[QStringLiteral("adbFailed")] = static_cast<double>(s.adbFailed);
    o[QStringLiteral("lastConnectLatencyMs")] = static_cast<double>(s.lastConnectLatencyMs);
    o[QStringLiteral("avgConnectLatencyMs")] = s.avgConnectLatencyMs;
    o[QStringLiteral("reconnects")] = s.reconnects;
    o[QStringLiteral("disconnects")] = s.disconnects;
    o[QStringLiteral("lastScanMs")] = static_cast<double>(s.lastScanMs);
    o[QStringLiteral("automationJobs")] = s.automationJobs;
    o[QStringLiteral("automationQueue")] = s.automationQueue;
    o[QStringLiteral("batchJobs")] = s.batchJobs;
    o[QStringLiteral("uiLagMaxMs")] = s.uiLagMaxMs;
    o[QStringLiteral("uiLagAvgMs")] = s.uiLagAvgMs;
    o[QStringLiteral("connectQueue")] = s.connectQueue;
    o[QStringLiteral("connectStarting")] = s.connectStarting;
    return o;
}
} // namespace

QString PerfMonitor::toJson(bool includeHistory) const
{
    QJsonObject root;
    root[QStringLiteral("current")] = snapshotToJson(m_current);
    root[QStringLiteral("cores")] = m_cores;
    if (includeHistory) {
        QJsonArray arr;
        for (const PerfSnapshot &s : m_history) {
            arr.append(snapshotToJson(s));
        }
        root[QStringLiteral("history")] = arr;
    }
    return QString::fromUtf8(QJsonDocument(root).toJson(QJsonDocument::Indented));
}

bool PerfMonitor::exportJson(const QString &path) const
{
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return false;
    }
    f.write(toJson(true).toUtf8());
    return true;
}

} // namespace farm
