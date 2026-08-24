#ifndef FARM_PERFORMANCE_PERFMONITOR_H
#define FARM_PERFORMANCE_PERFMONITOR_H

#include <atomic>

#include <QDateTime>
#include <QElapsedTimer>
#include <QList>
#include <QObject>
#include <QTimer>

namespace farm {

struct PerfSnapshot
{
    QDateTime time;
    double cpuPercent = 0;          // of the whole machine (all logical cores)
    double cpuPercentOneCore = 0;   // of one core
    qint64 workingSetMb = 0;
    qint64 privateMb = 0;
    int threads = 0;
    int handles = 0;
    double gpuPercent = -1;         // -1 = not available
    int devicesKnown = 0;
    int devicesOnline = 0;
    int mirroring = 0;
    double decodedFps = 0;          // frames delivered by decoders (all devices)
    double renderedFps = 0;         // frames uploaded to the GPU
    double droppedFps = 0;          // frames skipped (off-screen throttling / stale)
    double avgFrameHandleMs = -1;   // onFrame -> upload done (GUI thread cost per frame)
    double avgDisplayLatencyMs = -1;// onFrame -> paintGL finished
    double adbOpsPerSec = 0;
    double adbAvgMs = -1;
    int adbActive = 0;
    int adbQueued = 0;
    qint64 adbTotal = 0;
    qint64 adbFailed = 0;
    qint64 lastConnectLatencyMs = -1;
    double avgConnectLatencyMs = -1;
    int reconnects = 0;
    int disconnects = 0;
    qint64 lastScanMs = -1;
    int automationJobs = 0;
    int automationQueue = 0;
    int batchJobs = 0;
    int uiLagMaxMs = 0;             // worst event-loop stall in the interval
    double uiLagAvgMs = 0;
    int connectQueue = 0;
    int connectStarting = 0;
};

/**
 * Samples process + farm metrics once per interval (Settings › Performance) and
 * keeps a bounded history for the Performance page / diagnostics export.
 * The frame counters are fed from the render path (cheap atomics).
 */
class PerfMonitor : public QObject
{
    Q_OBJECT
public:
    static PerfMonitor &instance();

    void start();
    void stop();
    PerfSnapshot current() const { return m_current; }
    QList<PerfSnapshot> history() const { return m_history; }
    void setHistoryLength(int samples) { m_historyLength = samples; }

    // ---- counters fed by the render path ----
    void countDecoded() { m_decoded.fetch_add(1, std::memory_order_relaxed); }
    void countRendered(qint64 handleUs)
    {
        m_rendered.fetch_add(1, std::memory_order_relaxed);
        m_handleUs.fetch_add(handleUs, std::memory_order_relaxed);
    }
    void countDropped() { m_dropped.fetch_add(1, std::memory_order_relaxed); }
    void countDisplayed(qint64 latencyUs)
    {
        m_displayed.fetch_add(1, std::memory_order_relaxed);
        m_displayUs.fetch_add(latencyUs, std::memory_order_relaxed);
    }

    QString toJson(bool includeHistory) const;
    bool exportJson(const QString &path) const;

signals:
    void sampled(const farm::PerfSnapshot &snapshot);

private:
    explicit PerfMonitor(QObject *parent = nullptr);
    void sample();
    void probeUiLag();

    QTimer m_timer;
    QTimer m_lagTimer;
    QElapsedTimer m_lagClock;
    QElapsedTimer m_intervalClock;
    QList<PerfSnapshot> m_history;
    PerfSnapshot m_current;
    int m_historyLength = 600;
    std::atomic<qint64> m_decoded{0};
    std::atomic<qint64> m_rendered{0};
    std::atomic<qint64> m_dropped{0};
    std::atomic<qint64> m_displayed{0};
    std::atomic<qint64> m_handleUs{0};
    std::atomic<qint64> m_displayUs{0};
    qint64 m_lastAdbTotal = 0;
    qint64 m_lastAdbDurationMs = 0;
    quint64 m_lastProcTime100ns = 0;
    qint64 m_lastWallMs = 0;
    int m_lagMax = 0;
    qint64 m_lagSum = 0;
    int m_lagSamples = 0;
    int m_cores = 1;
};

} // namespace farm

Q_DECLARE_METATYPE(farm::PerfSnapshot)

#endif // FARM_PERFORMANCE_PERFMONITOR_H
