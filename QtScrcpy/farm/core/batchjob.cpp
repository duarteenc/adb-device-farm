#include "batchjob.h"

#include <algorithm>

#include <QPointer>
#include <QThread>

#include "activitylog.h"
#include "farmlog.h"

namespace farm {

BatchJob::BatchJob(const QString &name, const QStringList &ids, ItemFn fn, int concurrency, QObject *parent)
    : QObject(parent)
    , m_id(QUuid::createUuid().toString(QUuid::WithoutBraces))
    , m_name(name)
    , m_fn(std::move(fn))
    , m_concurrency(std::max(1, concurrency))
{
    qRegisterMetaType<farm::BatchJob::Status>("farm::BatchJob::Status");
    for (const QString &id : ids) {
        Item item;
        item.id = id;
        m_items.append(item);
    }
}

BatchJob::~BatchJob() = default;

QString BatchJob::statusName(Status status)
{
    switch (status) {
    case Pending:
        return QStringLiteral("Pending");
    case Running:
        return QStringLiteral("Running");
    case Paused:
        return QStringLiteral("Paused");
    case Completed:
        return QStringLiteral("Completed");
    case Failed:
        return QStringLiteral("Failed");
    case Cancelled:
        return QStringLiteral("Cancelled");
    }
    return QString();
}

QString BatchJob::itemStatusName(ItemStatus status)
{
    switch (status) {
    case Queued:
        return QStringLiteral("queued");
    case InProgress:
        return QStringLiteral("running");
    case Succeeded:
        return QStringLiteral("ok");
    case Failed_:
        return QStringLiteral("failed");
    case Cancelled_:
        return QStringLiteral("cancelled");
    case Skipped:
        return QStringLiteral("skipped");
    }
    return QString();
}

int BatchJob::queued() const
{
    int n = 0;
    for (const Item &i : m_items) {
        n += i.status == Queued ? 1 : 0;
    }
    return n;
}

int BatchJob::running() const
{
    int n = 0;
    for (const Item &i : m_items) {
        n += i.status == InProgress ? 1 : 0;
    }
    return n;
}

int BatchJob::succeeded() const
{
    int n = 0;
    for (const Item &i : m_items) {
        n += i.status == Succeeded ? 1 : 0;
    }
    return n;
}

int BatchJob::failed() const
{
    int n = 0;
    for (const Item &i : m_items) {
        n += i.status == Failed_ ? 1 : 0;
    }
    return n;
}

int BatchJob::cancelled() const
{
    int n = 0;
    for (const Item &i : m_items) {
        n += (i.status == Cancelled_ || i.status == Skipped) ? 1 : 0;
    }
    return n;
}

BatchJob::Item BatchJob::item(const QString &id) const
{
    for (const Item &i : m_items) {
        if (i.id == id) {
            return i;
        }
    }
    return Item();
}

BatchJob::Item *BatchJob::find(const QString &id)
{
    for (Item &i : m_items) {
        if (i.id == id) {
            return &i;
        }
    }
    return nullptr;
}

QStringList BatchJob::failedIds() const
{
    QStringList list;
    for (const Item &i : m_items) {
        if (i.status == Failed_) {
            list << i.id;
        }
    }
    return list;
}

qint64 BatchJob::elapsedMs() const
{
    if (!m_startedAt.isValid()) {
        return 0;
    }
    const QDateTime end = m_finishedAt.isValid() ? m_finishedAt : QDateTime::currentDateTime();
    return m_startedAt.msecsTo(end);
}

QString BatchJob::summary() const
{
    return QStringLiteral("%1 / %2 complete · %3 ok · %4 failed · %5 remaining")
        .arg(finishedCount())
        .arg(total())
        .arg(succeeded())
        .arg(failed())
        .arg(queued() + running());
}

void BatchJob::setStatus(Status status)
{
    if (m_status == status) {
        return;
    }
    m_status = status;
    emit statusChanged(status);
    if (status == Completed || status == Failed || status == Cancelled) {
        m_finishedAt = QDateTime::currentDateTime();
        const ActivityEntry::Level level = status == Completed ? ActivityEntry::Info : ActivityEntry::Warning;
        ActivityLog::instance().post(level, ActivityEntry::Device, QStringLiteral("%1: %2 (%3)").arg(m_name, summary(), statusName(status)));
        emit finished(status);
    }
}

void BatchJob::start()
{
    if (m_status == Running) {
        return;
    }
    if (!m_startedAt.isValid()) {
        m_startedAt = QDateTime::currentDateTime();
        ActivityLog::instance().info(ActivityEntry::Device, QStringLiteral("%1 started on %2 device(s)").arg(m_name).arg(total()));
    }
    m_token.reset();
    setStatus(Running);
    pump();
}

void BatchJob::pause()
{
    if (m_status == Running) {
        setStatus(Paused);
    }
}

void BatchJob::resume()
{
    if (m_status == Paused) {
        setStatus(Running);
        pump();
    }
}

void BatchJob::cancel()
{
    if (m_status != Running && m_status != Paused && m_status != Pending) {
        return;
    }
    m_token.cancel();
    for (Item &i : m_items) {
        if (i.status == Queued) {
            i.status = Cancelled_;
            emit itemChanged(i.id);
        }
    }
    if (running() == 0) {
        setStatus(Cancelled);
    } else {
        // in-flight items finish (or observe the token) and pump() closes the job
        m_status = Cancelled;
        emit statusChanged(Cancelled);
    }
    emit progressChanged();
}

void BatchJob::retryFailed()
{
    bool any = false;
    for (Item &i : m_items) {
        if (i.status == Failed_ || i.status == Cancelled_) {
            i.status = Queued;
            i.message.clear();
            any = true;
            emit itemChanged(i.id);
        }
    }
    if (!any) {
        return;
    }
    m_finishedAt = QDateTime();
    m_token.reset();
    setStatus(Running);
    emit progressChanged();
    pump();
}

void BatchJob::pump()
{
    if (m_status != Running) {
        if ((m_status == Cancelled) && running() == 0) {
            m_finishedAt = QDateTime::currentDateTime();
            emit finished(Cancelled);
        }
        return;
    }
    while (running() < m_concurrency) {
        Item *next = nullptr;
        for (Item &i : m_items) {
            if (i.status == Queued) {
                next = &i;
                break;
            }
        }
        if (!next) {
            break;
        }
        next->status = InProgress;
        next->started = QDateTime::currentDateTime();
        next->attempts += 1;
        const QString id = next->id;
        const int attempt = next->attempts;
        emit itemChanged(id);
        emit progressChanged();
        QPointer<BatchJob> self(this);
        DoneFn done = [self, id, attempt](bool ok, const QString &message) {
            if (!self) {
                return;
            }
            QMetaObject::invokeMethod(self.data(), [self, id, attempt, ok, message]() {
                if (self) {
                    self->onItemDone(id, attempt, ok, message);
                }
            }, Qt::QueuedConnection);
        };
        m_fn(id, m_token, done);
    }
    if (running() == 0 && queued() == 0) {
        setStatus(failed() == 0 ? Completed : (succeeded() == 0 && total() > 0 ? Failed : Completed));
    }
    emit progressChanged();
}

void BatchJob::onItemDone(const QString &id, int attempt, bool ok, const QString &message)
{
    Item *item = find(id);
    if (!item || item->attempts != attempt || item->status != InProgress) {
        return;    // stale completion (retry already started)
    }
    item->status = ok ? Succeeded : (m_token.isCancelled() && !ok ? Cancelled_ : Failed_);
    item->message = message;
    item->durationMs = item->started.msecsTo(QDateTime::currentDateTime());
    if (!ok) {
        FarmLog::instance().warning(QStringLiteral("batch"), QStringLiteral("%1 failed: %2").arg(m_name, message), id);
    }
    emit itemChanged(id);
    pump();
}

// ---------------------------------------------------------------- JobManager

JobManager &JobManager::instance()
{
    static JobManager manager;
    return manager;
}

JobManager::JobManager(QObject *parent)
    : QObject(parent)
{
}

void JobManager::add(BatchJob *job)
{
    if (!job || m_jobs.contains(job)) {
        return;
    }
    job->setParent(this);
    m_jobs.prepend(job);
    connect(job, &BatchJob::progressChanged, this, &JobManager::jobsChanged);
    connect(job, &BatchJob::statusChanged, this, [this](BatchJob::Status) { emit jobsChanged(); });
    while (m_jobs.size() > 100) {
        BatchJob *old = m_jobs.last();
        if (old->status() == BatchJob::Running || old->status() == BatchJob::Paused) {
            break;
        }
        m_jobs.removeLast();
        emit jobRemoved(old->id());
        old->deleteLater();
    }
    emit jobAdded(job);
    emit jobsChanged();
}

BatchJob *JobManager::startJob(BatchJob *job)
{
    add(job);
    job->start();
    return job;
}

QList<BatchJob *> JobManager::activeJobs() const
{
    QList<BatchJob *> list;
    for (BatchJob *j : m_jobs) {
        if (j->status() == BatchJob::Running || j->status() == BatchJob::Paused || j->status() == BatchJob::Pending) {
            list << j;
        }
    }
    return list;
}

BatchJob *JobManager::job(const QString &id) const
{
    for (BatchJob *j : m_jobs) {
        if (j->id() == id) {
            return j;
        }
    }
    return nullptr;
}

void JobManager::remove(BatchJob *job)
{
    if (!job || !m_jobs.removeOne(job)) {
        return;
    }
    emit jobRemoved(job->id());
    job->deleteLater();
    emit jobsChanged();
}

void JobManager::clearFinished()
{
    const QList<BatchJob *> copy = m_jobs;
    for (BatchJob *j : copy) {
        if (j->status() == BatchJob::Completed || j->status() == BatchJob::Failed || j->status() == BatchJob::Cancelled) {
            remove(j);
        }
    }
}

int JobManager::queueDepth() const
{
    int n = 0;
    for (BatchJob *j : m_jobs) {
        if (j->status() == BatchJob::Running || j->status() == BatchJob::Paused) {
            n += j->queued();
        }
    }
    return n;
}

} // namespace farm
