#ifndef FARM_CORE_TASKEXECUTOR_H
#define FARM_CORE_TASKEXECUTOR_H

#include <atomic>
#include <functional>
#include <memory>

#include <QHash>
#include <QMutex>
#include <QObject>
#include <QString>
#include <QThreadPool>

namespace farm {

/**
 * Cooperative cancellation flag shared between a job owner and its workers.
 * Copyable (shared state); `isCancelled()` is cheap enough to poll in loops.
 */
class CancellationToken
{
public:
    CancellationToken()
        : m_state(std::make_shared<std::atomic<bool>>(false))
    {
    }
    void cancel() { m_state->store(true); }
    bool isCancelled() const { return m_state->load(); }
    void reset() { m_state->store(false); }

private:
    std::shared_ptr<std::atomic<bool>> m_state;
};

/**
 * Central bounded executors ("lanes"). Nothing in the farm may spawn one unbounded
 * thread per device: expensive work is submitted to a named lane whose concurrency
 * is capped (and configurable through Settings):
 *
 *   adb        - external adb.exe invocations
 *   connect    - device connection setups (server push + mirror start)
 *   network    - subnet probes, mDNS, ARP
 *   automation - workflow device runs
 *   media      - screenshot encoding, image matching, OCR, recordings
 *   io         - file transfers, database maintenance
 *
 * Results should be delivered back to QObjects via QMetaObject::invokeMethod with
 * a context object so they land on the owner's thread.
 */
class TaskExecutor : public QObject
{
    Q_OBJECT
public:
    static TaskExecutor &instance();

    QThreadPool *lane(const QString &name);
    void setLaneConcurrency(const QString &name, int maxThreads);
    int laneConcurrency(const QString &name) const;

    /// Run `fn` on the given lane. Returns false if the lane does not exist.
    bool run(const QString &laneName, std::function<void()> fn);

    /// Active + queued work per lane (for the Performance page).
    int activeCount(const QString &name) const;
    int queuedCount(const QString &name) const;

    /// Stop accepting work and wait for all lanes (used at shutdown).
    void shutdown(int waitMs = 5000);

private:
    explicit TaskExecutor(QObject *parent = nullptr);
    ~TaskExecutor() override;

    struct Lane
    {
        QThreadPool *pool = nullptr;
        std::atomic<int> queued{0};
    };
    QHash<QString, Lane *> m_lanes;
    mutable QMutex m_mutex;
    bool m_shuttingDown = false;
};

} // namespace farm

#endif // FARM_CORE_TASKEXECUTOR_H
