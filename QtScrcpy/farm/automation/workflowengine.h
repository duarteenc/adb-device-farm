#ifndef FARM_AUTOMATION_WORKFLOWENGINE_H
#define FARM_AUTOMATION_WORKFLOWENGINE_H

#include <atomic>

#include <QDateTime>
#include <QHash>
#include <QList>
#include <QMutex>
#include <QObject>
#include <QStringList>

#include "../core/taskexecutor.h"
#include "../storage/repositories.h"
#include "workflowmodel.h"

namespace farm {

/**
 * One execution of a workflow over a set of devices. Each device runs in its
 * own worker (automation lane), at most `concurrency` at a time. Failures on
 * one device never affect the others. Pause/Resume/Stop/Cancel/Retry-failed.
 *
 * Every step produces a structured log row (timestamp, device, step, status,
 * duration, message, error, screenshot) persisted in the database and written
 * as <runs>/<run>/logs.json at the end; failures capture an error screenshot
 * under <runs>/<run>/<device>/error-step-N.png.
 */
class AutomationRun : public QObject
{
    Q_OBJECT
public:
    enum Status { Pending, Running, Paused, Completed, Failed, Cancelled };

    struct DeviceProgress
    {
        QString id;
        QString status = QStringLiteral("queued");    // queued running ok failed cancelled
        QString currentNode;
        QString currentTitle;
        int steps = 0;
        QString error;
        QString errorScreenshot;
        qint64 startedMs = 0;
        qint64 finishedMs = 0;
        int attempts = 0;
    };

    AutomationRun(const Workflow &workflow, const QStringList &targets, int concurrency, const QString &triggeredBy, QObject *parent = nullptr);
    ~AutomationRun() override;

    QString id() const { return m_id; }
    QString name() const { return m_workflow.name; }
    const Workflow &workflow() const { return m_workflow; }
    Status status() const { return m_status; }
    static QString statusName(Status s);
    QStringList targets() const { return m_targets; }
    int concurrency() const { return m_concurrency; }
    QString triggeredBy() const { return m_triggeredBy; }
    QString runDirectory() const { return m_runDir; }
    QDateTime startedAt() const { return m_startedAt; }
    QDateTime finishedAt() const { return m_finishedAt; }
    DeviceProgress progress(const QString &deviceId) const;
    QList<DeviceProgress> allProgress() const;
    int total() const { return static_cast<int>(m_targets.size()); }
    int succeeded() const;
    int failed() const;
    int running() const;
    int queued() const;
    int percent() const;
    QString summary() const;
    QList<JobLogRow> logs() const;
    QStringList failedIds() const;

    // ---- engine-internal (worker threads) ----
    bool isCancelled() const { return m_token.isCancelled(); }
    bool isStopRequested() const { return m_stopRequested.load(); }
    bool isPaused() const { return m_paused.load(); }
    CancellationToken token() const { return m_token; }
    void reportProgress(const QString &deviceId, const QString &nodeId, const QString &title, int steps);
    void reportLog(const JobLogRow &row);
    void reportDeviceFinished(const QString &deviceId, bool ok, const QString &error, const QString &screenshot);

public slots:
    void start();
    void pause();
    void resume();
    void stop();      // finish the current step on every device, start nothing new
    void cancel();    // abort immediately
    void retryFailed();

signals:
    void statusChanged(farm::AutomationRun::Status status);
    void deviceChanged(const QString &deviceId);
    void logAppended(const farm::JobLogRow &row);
    void finished(farm::AutomationRun::Status status);

private:
    void pump();
    void setStatus(Status s);
    void persistRun();
    void writeLogsJson();

    QString m_id;
    Workflow m_workflow;
    QStringList m_targets;
    int m_concurrency = 5;
    QString m_triggeredBy;
    QString m_runDir;
    Status m_status = Pending;
    QDateTime m_startedAt;
    QDateTime m_finishedAt;
    QHash<QString, DeviceProgress> m_progress;
    QList<JobLogRow> m_logs;
    mutable QMutex m_mutex;
    CancellationToken m_token;
    std::atomic<bool> m_paused{false};
    std::atomic<bool> m_stopRequested{false};
};

/**
 * Creates and tracks AutomationRuns; resolves target lists; loads workflows by
 * name or id so runs can be started from the UI, the scheduler, the CLI or
 * another workflow.
 */
class WorkflowEngine : public QObject
{
    Q_OBJECT
public:
    static WorkflowEngine &instance();

    AutomationRun *start(const Workflow &workflow, const QStringList &targets, int concurrency, const QString &triggeredBy);
    QList<AutomationRun *> runs() const { return m_runs; }
    QList<AutomationRun *> activeRuns() const;
    AutomationRun *run(const QString &id) const;
    void remove(AutomationRun *run);
    void clearFinished();
    int activeCount() const { return static_cast<int>(activeRuns().size()); }

    /// targetsMode: selection | group | all | online | devices
    static QStringList resolveTargets(const QString &mode, const QString &group, const QStringList &selection);
    static Workflow loadWorkflow(const QString &nameOrId, bool *found = nullptr);
    static QStringList workflowNames();

signals:
    void runAdded(farm::AutomationRun *run);
    void runRemoved(const QString &id);
    void runsChanged();

private:
    explicit WorkflowEngine(QObject *parent = nullptr);
    QList<AutomationRun *> m_runs;
};

} // namespace farm

Q_DECLARE_METATYPE(farm::AutomationRun::Status)
Q_DECLARE_METATYPE(farm::JobLogRow)

#endif // FARM_AUTOMATION_WORKFLOWENGINE_H
