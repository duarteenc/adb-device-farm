#ifndef FARM_CORE_BATCHJOB_H
#define FARM_CORE_BATCHJOB_H

#include <functional>

#include <QDateTime>
#include <QHash>
#include <QList>
#include <QObject>
#include <QStringList>
#include <QUuid>

#include "taskexecutor.h"

namespace farm {

/**
 * A batch operation over many devices with bounded concurrency, cancellation,
 * per-item results and "retry failed only".
 *
 * The item function is asynchronous: it must call `done(ok, message)` exactly
 * once (from any thread). Typical implementations chain AdbExecutor calls.
 * BatchJob itself lives on the GUI thread and marshals completions back to it.
 */
class BatchJob : public QObject
{
    Q_OBJECT
public:
    enum Status { Pending, Running, Paused, Completed, Failed, Cancelled };
    enum ItemStatus { Queued, InProgress, Succeeded, Failed_, Cancelled_, Skipped };

    struct Item
    {
        QString id;
        ItemStatus status = Queued;
        QString message;
        qint64 durationMs = 0;
        QDateTime started;
        int attempts = 0;
    };

    using DoneFn = std::function<void(bool ok, const QString &message)>;
    using ItemFn = std::function<void(const QString &id, CancellationToken token, DoneFn done)>;

    BatchJob(const QString &name, const QStringList &ids, ItemFn fn, int concurrency, QObject *parent = nullptr);
    ~BatchJob() override;

    QString id() const { return m_id; }
    QString name() const { return m_name; }
    QString kind() const { return m_kind; }
    void setKind(const QString &kind) { m_kind = kind; }
    void setDestructive(bool destructive) { m_destructive = destructive; }
    bool isDestructive() const { return m_destructive; }
    Status status() const { return m_status; }
    static QString statusName(Status status);
    static QString itemStatusName(ItemStatus status);

    int total() const { return static_cast<int>(m_items.size()); }
    int queued() const;
    int running() const;
    int succeeded() const;
    int failed() const;
    int cancelled() const;
    int finishedCount() const { return succeeded() + failed() + cancelled(); }
    int percent() const { return total() == 0 ? 100 : finishedCount() * 100 / total(); }
    QList<Item> items() const { return m_items; }
    Item item(const QString &id) const;
    QStringList failedIds() const;
    QDateTime startedAt() const { return m_startedAt; }
    QDateTime finishedAt() const { return m_finishedAt; }
    qint64 elapsedMs() const;
    QString summary() const;

public slots:
    void start();
    void pause();
    void resume();
    void cancel();
    void retryFailed();

signals:
    void progressChanged();
    void itemChanged(const QString &id);
    void statusChanged(farm::BatchJob::Status status);
    void finished(farm::BatchJob::Status status);

private:
    void pump();
    void onItemDone(const QString &id, int attempt, bool ok, const QString &message);
    void setStatus(Status status);
    Item *find(const QString &id);

    QString m_id;
    QString m_name;
    QString m_kind = QStringLiteral("batch");
    QList<Item> m_items;
    ItemFn m_fn;
    int m_concurrency = 4;
    Status m_status = Pending;
    CancellationToken m_token;
    QDateTime m_startedAt;
    QDateTime m_finishedAt;
    bool m_destructive = false;
};

/**
 * Registry of live and recently finished jobs (batch + automation) for the
 * Dashboard / Activity views. Owns the jobs.
 */
class JobManager : public QObject
{
    Q_OBJECT
public:
    static JobManager &instance();

    void add(BatchJob *job);              // takes ownership; does not start it
    BatchJob *startJob(BatchJob *job);    // add + start
    QList<BatchJob *> jobs() const { return m_jobs; }
    QList<BatchJob *> activeJobs() const;
    BatchJob *job(const QString &id) const;
    void remove(BatchJob *job);
    void clearFinished();
    int activeCount() const { return static_cast<int>(activeJobs().size()); }
    int queueDepth() const;

signals:
    void jobAdded(farm::BatchJob *job);
    void jobRemoved(const QString &id);
    void jobsChanged();

private:
    explicit JobManager(QObject *parent = nullptr);
    QList<BatchJob *> m_jobs;
};

} // namespace farm

Q_DECLARE_METATYPE(farm::BatchJob::Status)

#endif // FARM_CORE_BATCHJOB_H
