#include "taskexecutor.h"

#include <algorithm>

#include <QRunnable>
#include <QThread>

namespace farm {

namespace {
struct LaneSpec
{
    const char *name;
    int threads;
};
// Defaults are deliberately conservative for a 2-4 core operator PC; the Settings
// page can raise them. `adb` is the widest because most calls are I/O bound.
const LaneSpec kLanes[] = {
    { "adb", 8 }, { "connect", 4 }, { "network", 4 }, { "automation", 5 }, { "media", 2 }, { "io", 2 },
};
} // namespace

TaskExecutor &TaskExecutor::instance()
{
    static TaskExecutor executor;
    return executor;
}

TaskExecutor::TaskExecutor(QObject *parent)
    : QObject(parent)
{
    for (const LaneSpec &spec : kLanes) {
        auto *lane = new Lane;
        lane->pool = new QThreadPool(this);
        lane->pool->setObjectName(QStringLiteral("lane-") + QLatin1String(spec.name));
        lane->pool->setMaxThreadCount(spec.threads);
        lane->pool->setExpiryTimeout(30000);
        m_lanes.insert(QLatin1String(spec.name), lane);
    }
}

TaskExecutor::~TaskExecutor()
{
    shutdown(2000);
    qDeleteAll(m_lanes);
}

QThreadPool *TaskExecutor::lane(const QString &name)
{
    QMutexLocker lock(&m_mutex);
    Lane *lane = m_lanes.value(name, nullptr);
    return lane ? lane->pool : nullptr;
}

void TaskExecutor::setLaneConcurrency(const QString &name, int maxThreads)
{
    QMutexLocker lock(&m_mutex);
    if (Lane *lane = m_lanes.value(name, nullptr)) {
        lane->pool->setMaxThreadCount(std::max(1, maxThreads));
    }
}

int TaskExecutor::laneConcurrency(const QString &name) const
{
    QMutexLocker lock(&m_mutex);
    Lane *lane = m_lanes.value(name, nullptr);
    return lane ? lane->pool->maxThreadCount() : 0;
}

bool TaskExecutor::run(const QString &laneName, std::function<void()> fn)
{
    Lane *lane = nullptr;
    {
        QMutexLocker lock(&m_mutex);
        if (m_shuttingDown) {
            return false;
        }
        lane = m_lanes.value(laneName, nullptr);
    }
    if (!lane) {
        return false;
    }
    lane->queued.fetch_add(1);
    // Worker threads get the lane's name so log lines identify them.
    lane->pool->start([lane, fn = std::move(fn)]() {
        lane->queued.fetch_sub(1);
        if (QThread::currentThread()->objectName().isEmpty()) {
            QThread::currentThread()->setObjectName(lane->pool->objectName());
        }
        fn();
    });
    return true;
}

int TaskExecutor::activeCount(const QString &name) const
{
    QMutexLocker lock(&m_mutex);
    Lane *lane = m_lanes.value(name, nullptr);
    return lane ? lane->pool->activeThreadCount() : 0;
}

int TaskExecutor::queuedCount(const QString &name) const
{
    QMutexLocker lock(&m_mutex);
    Lane *lane = m_lanes.value(name, nullptr);
    return lane ? lane->queued.load() : 0;
}

void TaskExecutor::shutdown(int waitMs)
{
    QList<Lane *> lanes;
    {
        QMutexLocker lock(&m_mutex);
        m_shuttingDown = true;
        lanes = m_lanes.values();
    }
    for (Lane *lane : lanes) {
        lane->pool->clear();
        if (!lane->pool->waitForDone(waitMs)) {
            // A worker is wedged in an uninterruptible call. ~QThreadPool would wait
            // forever and we are exiting anyway: detach the pool so teardown cannot hang.
            lane->pool->setParent(nullptr);
        }
    }
}

} // namespace farm
