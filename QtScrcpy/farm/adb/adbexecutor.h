#ifndef FARM_ADB_ADBEXECUTOR_H
#define FARM_ADB_ADBEXECUTOR_H

#include <atomic>
#include <functional>

#include <QByteArray>
#include <QHash>
#include <QMutex>
#include <QObject>
#include <QPointer>
#include <QQueue>
#include <QSemaphore>
#include <QString>
#include <QStringList>
#include <QUuid>

#include "../core/taskexecutor.h"

class QProcess;
class QThread;

namespace farm {

struct AdbCommand
{
    QString serial;             // empty = no -s
    QStringList args;           // e.g. {"shell", "getprop"}
    int timeoutMs = 15000;      // hard kill after this
    QByteArray stdinData;       // optional data piped to the process
    bool binaryOutput = false;  // keep stdout as raw bytes (exec-out screencap)
    QString label;              // free text for logs/metrics
};

struct AdbResult
{
    bool ok = false;            // exit code 0, not timed out, process started
    int exitCode = -1;
    QString stdOut;
    QString stdErr;
    QByteArray rawStdOut;       // filled when AdbCommand::binaryOutput
    QString error;              // human-readable failure reason
    qint64 durationMs = 0;
    bool timedOut = false;
    bool cancelled = false;
    QString combined() const { return stdErr.isEmpty() ? stdOut : (stdOut + QLatin1Char('\n') + stdErr); }
};

using AdbCallback = std::function<void(const AdbResult &)>;

/**
 * Central, bounded runner for external `adb.exe` invocations.
 *
 *  - Async API (`run`): the QProcess lives on the executor's own thread; the
 *    callback is delivered on `context`'s thread (queued) so UI code never blocks
 *    and never touches the process. Concurrency is capped (Settings › ADB) and
 *    excess commands queue in FIFO order (a per-serial fairness rule stops one
 *    hung phone from monopolising the slots).
 *  - Sync API (`runSync`): for worker threads only (asserts off the GUI thread);
 *    counts against the same concurrency cap through a semaphore.
 *  - Every command has a timeout, an exit code, stdout/stderr, a duration and can
 *    be cancelled by id. Metrics (ops/sec, avg round-trip) feed the Performance page.
 */
class AdbExecutor : public QObject
{
    Q_OBJECT
public:
    static AdbExecutor &instance();

    QString adbPath() const;
    void setAdbPath(const QString &path);
    void setMaxConcurrency(int n);
    int maxConcurrency() const { return m_maxConcurrency; }

    /// Start the executor thread (idempotent). Called by AppContext.
    void start();
    void stop();

    QUuid run(const AdbCommand &command, QObject *context, AdbCallback callback);
    QUuid run(const AdbCommand &command, QObject *context, AdbCallback callback, CancellationToken token);
    AdbResult runSync(const AdbCommand &command, CancellationToken token = CancellationToken());
    void cancel(const QUuid &id);
    void cancelAll();

    // ---- convenience wrappers (async) ----
    QUuid shell(const QString &serial, const QString &script, QObject *context, AdbCallback cb, int timeoutMs = 15000);
    QUuid devices(QObject *context, AdbCallback cb);
    QUuid connectEndpoint(const QString &endpoint, QObject *context, AdbCallback cb, int timeoutMs = 6000);
    QUuid disconnectEndpoint(const QString &endpoint, QObject *context, AdbCallback cb);

    // ---- metrics ----
    struct Metrics
    {
        qint64 total = 0;
        qint64 failed = 0;
        qint64 timedOut = 0;
        qint64 totalDurationMs = 0;
        int active = 0;
        int queued = 0;
    };
    Metrics metrics() const;

signals:
    void commandFinished(const QString &serial, const QString &label, bool ok, qint64 durationMs);

private:
    explicit AdbExecutor(QObject *parent = nullptr);
    ~AdbExecutor() override;

    struct Pending
    {
        QUuid id;
        AdbCommand command;
        QPointer<QObject> context;
        AdbCallback callback;
        CancellationToken token;
        bool hasToken = false;
        bool hadContext = false;    // a receiver was given: never call the callback if it died
    };
    struct Running
    {
        Pending pending;
        QProcess *process = nullptr;
        qint64 startedMs = 0;
    };

    QUuid enqueue(Pending pending);
    void kickPump();
    void releaseSlot();                   // returns a permit, honouring a pending cap reduction
    void pump();                          // executor thread: start queued processes up to the cap
    void launch(const Pending &pending);  // executor thread
    void finish(const QUuid &id, AdbResult result);
    void deliver(const Pending &pending, const AdbResult &result);
    QStringList buildArguments(const AdbCommand &command) const;
    void record(const AdbResult &result);

    QThread *m_thread = nullptr;
    QObject *m_worker = nullptr;          // lives on m_thread; owns the QProcess objects
    mutable QMutex m_mutex;
    QString m_adbPath;
    int m_maxConcurrency = 8;
    QQueue<Pending> m_queue;
    QHash<QUuid, Running *> m_running;
    QSemaphore m_slots;                   // shared cap for sync + async
    Metrics m_metrics;
    std::atomic<bool> m_started{false};
    std::atomic<bool> m_stopping{false};
    int m_pendingShrink = 0;              // permits still to be retired after setMaxConcurrency() lowered the cap
};

} // namespace farm

Q_DECLARE_METATYPE(farm::AdbResult)

#endif // FARM_ADB_ADBEXECUTOR_H
