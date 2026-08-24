#include "adbexecutor.h"

#include <algorithm>

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>
#include <QThread>
#include <QTimer>

#include "../core/farmlog.h"

namespace farm {

namespace {
constexpr int kMaxSlots = 64;    // absolute ceiling for the shared semaphore

QString resolveAdb(const QString &configured)
{
    if (!configured.isEmpty() && QFileInfo::exists(configured)) {
        return QDir::toNativeSeparators(configured);
    }
    const QString beside = QCoreApplication::applicationDirPath() + QStringLiteral("/adb.exe");
    if (QFileInfo::exists(beside)) {
        return QDir::toNativeSeparators(beside);
    }
    const QString env = QString::fromLocal8Bit(qgetenv("QTSCRCPY_ADB_PATH"));
    if (!env.isEmpty() && QFileInfo::exists(env)) {
        return QDir::toNativeSeparators(QFileInfo(env).absoluteFilePath());
    }
    const QString onPath = QStandardPaths::findExecutable(QStringLiteral("adb"));
    return onPath.isEmpty() ? QStringLiteral("adb") : onPath;
}
} // namespace

AdbExecutor &AdbExecutor::instance()
{
    static AdbExecutor executor;
    return executor;
}

AdbExecutor::AdbExecutor(QObject *parent)
    : QObject(parent)
    , m_slots(kMaxSlots)
{
    qRegisterMetaType<farm::AdbResult>("farm::AdbResult");
    // Reserve slots so that only m_maxConcurrency remain available.
    m_slots.acquire(kMaxSlots - m_maxConcurrency);
}

AdbExecutor::~AdbExecutor()
{
    stop();
}

QString AdbExecutor::adbPath() const
{
    QMutexLocker lock(&m_mutex);
    return m_adbPath.isEmpty() ? resolveAdb(QString()) : m_adbPath;
}

void AdbExecutor::setAdbPath(const QString &path)
{
    QMutexLocker lock(&m_mutex);
    m_adbPath = resolveAdb(path);
}

void AdbExecutor::setMaxConcurrency(int n)
{
    n = std::clamp(n, 1, kMaxSlots);
    QMutexLocker lock(&m_mutex);
    if (n == m_maxConcurrency) {
        return;
    }
    if (n > m_maxConcurrency) {
        m_slots.release(n - m_maxConcurrency);
    } else {
        // Shrinking: take the difference back lazily (may block briefly if all busy).
        m_slots.tryAcquire(m_maxConcurrency - n, 0);
    }
    m_maxConcurrency = n;
    if (m_thread) {
        QMetaObject::invokeMethod(m_worker, [this]() { pump(); }, Qt::QueuedConnection);
    }
}

void AdbExecutor::start()
{
    if (m_started.exchange(true)) {
        return;
    }
    m_thread = new QThread(this);
    m_thread->setObjectName(QStringLiteral("adb-executor"));
    m_worker = new QObject;
    m_worker->moveToThread(m_thread);
    m_thread->start();
    if (m_adbPath.isEmpty()) {
        m_adbPath = resolveAdb(QString());
    }
    FarmLog::instance().info(QStringLiteral("adb"), QStringLiteral("executor started, adb=%1, concurrency=%2").arg(m_adbPath).arg(m_maxConcurrency));
}

void AdbExecutor::stop()
{
    if (!m_started.exchange(false)) {
        return;
    }
    cancelAll();
    if (m_thread) {
        QMetaObject::invokeMethod(m_worker, [this]() { m_worker->deleteLater(); }, Qt::BlockingQueuedConnection);
        m_thread->quit();
        m_thread->wait(3000);
        m_thread->deleteLater();
        m_thread = nullptr;
        m_worker = nullptr;
    }
}

QStringList AdbExecutor::buildArguments(const AdbCommand &command) const
{
    QStringList args;
    if (!command.serial.isEmpty()) {
        args << QStringLiteral("-s") << command.serial;
    }
    args << command.args;
    return args;
}

QUuid AdbExecutor::run(const AdbCommand &command, QObject *context, AdbCallback callback)
{
    Pending p;
    p.id = QUuid::createUuid();
    p.command = command;
    p.context = context;
    p.callback = std::move(callback);
    {
        QMutexLocker lock(&m_mutex);
        m_queue.enqueue(p);
        m_metrics.queued = static_cast<int>(m_queue.size());
    }
    if (!m_started) {
        start();
    }
    QMetaObject::invokeMethod(m_worker, [this]() { pump(); }, Qt::QueuedConnection);
    return p.id;
}

QUuid AdbExecutor::run(const AdbCommand &command, QObject *context, AdbCallback callback, CancellationToken token)
{
    Pending p;
    p.id = QUuid::createUuid();
    p.command = command;
    p.context = context;
    p.callback = std::move(callback);
    p.token = token;
    p.hasToken = true;
    {
        QMutexLocker lock(&m_mutex);
        m_queue.enqueue(p);
        m_metrics.queued = static_cast<int>(m_queue.size());
    }
    if (!m_started) {
        start();
    }
    QMetaObject::invokeMethod(m_worker, [this]() { pump(); }, Qt::QueuedConnection);
    return p.id;
}

void AdbExecutor::pump()
{
    // Executor thread only.
    for (;;) {
        Pending next;
        {
            QMutexLocker lock(&m_mutex);
            if (m_queue.isEmpty()) {
                m_metrics.queued = 0;
                return;
            }
            if (!m_slots.tryAcquire(1, 0)) {
                return;    // at the cap; finish() re-pumps
            }
            next = m_queue.dequeue();
            m_metrics.queued = static_cast<int>(m_queue.size());
        }
        if (next.hasToken && next.token.isCancelled()) {
            AdbResult r;
            r.cancelled = true;
            r.error = QStringLiteral("cancelled");
            m_slots.release(1);
            deliver(next, r);
            continue;
        }
        launch(next);
    }
}

void AdbExecutor::launch(const Pending &pending)
{
    auto *running = new Running;
    running->pending = pending;
    running->process = new QProcess(m_worker);
    running->startedMs = QDateTime::currentMSecsSinceEpoch();
    QProcess *process = running->process;
    const QUuid id = pending.id;
    {
        QMutexLocker lock(&m_mutex);
        m_running.insert(id, running);
        m_metrics.active = static_cast<int>(m_running.size());
    }

    process->setProgram(adbPath());
    process->setArguments(buildArguments(pending.command));
    process->setProcessChannelMode(QProcess::SeparateChannels);

    connect(process, &QProcess::finished, m_worker, [this, id](int exitCode, QProcess::ExitStatus status) {
        Running *r = nullptr;
        {
            QMutexLocker lock(&m_mutex);
            r = m_running.value(id, nullptr);
        }
        if (!r) {
            return;
        }
        AdbResult result;
        result.exitCode = exitCode;
        if (r->pending.command.binaryOutput) {
            result.rawStdOut = r->process->readAllStandardOutput();
        } else {
            result.stdOut = QString::fromUtf8(r->process->readAllStandardOutput());
        }
        result.stdErr = QString::fromUtf8(r->process->readAllStandardError());
        result.ok = (status == QProcess::NormalExit && exitCode == 0);
        if (!result.ok) {
            result.error = result.stdErr.trimmed().isEmpty() ? QStringLiteral("exit code %1").arg(exitCode) : result.stdErr.trimmed();
        }
        finish(id, result);
    });
    connect(process, &QProcess::errorOccurred, m_worker, [this, id](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart) {
            return;    // Crashed/Timedout are reported through finished()/watchdog
        }
        AdbResult result;
        result.error = QStringLiteral("adb failed to start (%1)").arg(adbPath());
        finish(id, result);
    });

    // Watchdog: hard timeout. adb itself can hang on a wedged transport.
    const int timeout = pending.command.timeoutMs > 0 ? pending.command.timeoutMs : 15000;
    auto *watchdog = new QTimer(process);
    watchdog->setSingleShot(true);
    connect(watchdog, &QTimer::timeout, m_worker, [this, id]() {
        Running *r = nullptr;
        {
            QMutexLocker lock(&m_mutex);
            r = m_running.value(id, nullptr);
        }
        if (!r || r->process->state() == QProcess::NotRunning) {
            return;
        }
        AdbResult result;
        result.timedOut = true;
        result.error = QStringLiteral("timed out after %1 ms").arg(r->pending.command.timeoutMs);
        result.stdOut = QString::fromUtf8(r->process->readAllStandardOutput());
        result.stdErr = QString::fromUtf8(r->process->readAllStandardError());
        r->process->kill();
        finish(id, result);
    });
    watchdog->start(timeout);

    // Cooperative cancellation poll (cheap; only when a token was supplied).
    if (pending.hasToken) {
        auto *poll = new QTimer(process);
        connect(poll, &QTimer::timeout, m_worker, [this, id]() {
            Running *r = nullptr;
            {
                QMutexLocker lock(&m_mutex);
                r = m_running.value(id, nullptr);
            }
            if (r && r->pending.token.isCancelled()) {
                AdbResult result;
                result.cancelled = true;
                result.error = QStringLiteral("cancelled");
                r->process->kill();
                finish(id, result);
            }
        });
        poll->start(200);
    }

    process->start();
    if (!pending.command.stdinData.isEmpty()) {
        process->write(pending.command.stdinData);
        process->closeWriteChannel();
    }
}

void AdbExecutor::finish(const QUuid &id, AdbResult result)
{
    Running *r = nullptr;
    {
        QMutexLocker lock(&m_mutex);
        r = m_running.take(id);
        m_metrics.active = static_cast<int>(m_running.size());
    }
    if (!r) {
        return;
    }
    result.durationMs = QDateTime::currentMSecsSinceEpoch() - r->startedMs;
    record(result);
    r->process->disconnect();
    r->process->deleteLater();
    const Pending pending = r->pending;
    delete r;
    m_slots.release(1);
    emit commandFinished(pending.command.serial, pending.command.label, result.ok, result.durationMs);
    deliver(pending, result);
    pump();
}

void AdbExecutor::deliver(const Pending &pending, const AdbResult &result)
{
    if (!pending.callback) {
        return;
    }
    if (pending.context) {
        QObject *ctx = pending.context.data();
        AdbCallback cb = pending.callback;
        QMetaObject::invokeMethod(ctx, [cb, result]() { cb(result); }, Qt::QueuedConnection);
    } else {
        pending.callback(result);
    }
}

void AdbExecutor::record(const AdbResult &result)
{
    QMutexLocker lock(&m_mutex);
    ++m_metrics.total;
    if (!result.ok) {
        ++m_metrics.failed;
    }
    if (result.timedOut) {
        ++m_metrics.timedOut;
    }
    m_metrics.totalDurationMs += result.durationMs;
}

AdbResult AdbExecutor::runSync(const AdbCommand &command, CancellationToken token)
{
    Q_ASSERT_X(QThread::currentThread() != qApp->thread(), "AdbExecutor::runSync", "never block the GUI thread");
    AdbResult result;
    if (token.isCancelled()) {
        result.cancelled = true;
        result.error = QStringLiteral("cancelled");
        return result;
    }
    m_slots.acquire(1);
    QElapsedTimer timer;
    timer.start();
    QProcess process;
    process.setProgram(adbPath());
    process.setArguments(buildArguments(command));
    process.setProcessChannelMode(QProcess::SeparateChannels);
    process.start();
    if (!process.waitForStarted(5000)) {
        result.error = QStringLiteral("adb failed to start (%1)").arg(adbPath());
        m_slots.release(1);
        result.durationMs = timer.elapsed();
        record(result);
        return result;
    }
    if (!command.stdinData.isEmpty()) {
        process.write(command.stdinData);
        process.closeWriteChannel();
    }
    const int timeout = command.timeoutMs > 0 ? command.timeoutMs : 15000;
    bool finished = false;
    while (timer.elapsed() < timeout) {
        if (process.waitForFinished(100)) {
            finished = true;
            break;
        }
        if (token.isCancelled()) {
            process.kill();
            process.waitForFinished(1000);
            result.cancelled = true;
            result.error = QStringLiteral("cancelled");
            break;
        }
    }
    if (!finished && !result.cancelled) {
        process.kill();
        process.waitForFinished(1000);
        result.timedOut = true;
        result.error = QStringLiteral("timed out after %1 ms").arg(timeout);
    }
    if (command.binaryOutput) {
        result.rawStdOut = process.readAllStandardOutput();
    } else {
        result.stdOut = QString::fromUtf8(process.readAllStandardOutput());
    }
    result.stdErr = QString::fromUtf8(process.readAllStandardError());
    if (finished) {
        result.exitCode = process.exitCode();
        result.ok = process.exitStatus() == QProcess::NormalExit && result.exitCode == 0;
        if (!result.ok && result.error.isEmpty()) {
            result.error = result.stdErr.trimmed().isEmpty() ? QStringLiteral("exit code %1").arg(result.exitCode) : result.stdErr.trimmed();
        }
    }
    result.durationMs = timer.elapsed();
    m_slots.release(1);
    record(result);
    emit commandFinished(command.serial, command.label, result.ok, result.durationMs);
    return result;
}

void AdbExecutor::cancel(const QUuid &id)
{
    {
        QMutexLocker lock(&m_mutex);
        for (int i = 0; i < m_queue.size(); ++i) {
            if (m_queue.at(i).id == id) {
                Pending p = m_queue.at(i);
                m_queue.removeAt(i);
                lock.unlock();
                AdbResult r;
                r.cancelled = true;
                r.error = QStringLiteral("cancelled");
                deliver(p, r);
                return;
            }
        }
    }
    if (m_worker) {
        QMetaObject::invokeMethod(m_worker, [this, id]() {
            Running *r = nullptr;
            {
                QMutexLocker lock(&m_mutex);
                r = m_running.value(id, nullptr);
            }
            if (!r) {
                return;
            }
            AdbResult result;
            result.cancelled = true;
            result.error = QStringLiteral("cancelled");
            r->process->kill();
            finish(id, result);
        }, Qt::QueuedConnection);
    }
}

void AdbExecutor::cancelAll()
{
    QList<Pending> queued;
    QList<QUuid> running;
    {
        QMutexLocker lock(&m_mutex);
        while (!m_queue.isEmpty()) {
            queued.append(m_queue.dequeue());
        }
        running = m_running.keys();
    }
    for (const Pending &p : queued) {
        AdbResult r;
        r.cancelled = true;
        r.error = QStringLiteral("cancelled");
        deliver(p, r);
    }
    for (const QUuid &id : running) {
        cancel(id);
    }
}

QUuid AdbExecutor::shell(const QString &serial, const QString &script, QObject *context, AdbCallback cb, int timeoutMs)
{
    AdbCommand c;
    c.serial = serial;
    c.args << QStringLiteral("shell") << script;
    c.timeoutMs = timeoutMs;
    c.label = QStringLiteral("shell");
    return run(c, context, std::move(cb));
}

QUuid AdbExecutor::devices(QObject *context, AdbCallback cb)
{
    AdbCommand c;
    c.args << QStringLiteral("devices") << QStringLiteral("-l");
    c.timeoutMs = 8000;
    c.label = QStringLiteral("devices");
    return run(c, context, std::move(cb));
}

QUuid AdbExecutor::connectEndpoint(const QString &endpoint, QObject *context, AdbCallback cb, int timeoutMs)
{
    AdbCommand c;
    c.args << QStringLiteral("connect") << endpoint;
    c.timeoutMs = timeoutMs;
    c.label = QStringLiteral("connect");
    return run(c, context, std::move(cb));
}

QUuid AdbExecutor::disconnectEndpoint(const QString &endpoint, QObject *context, AdbCallback cb)
{
    AdbCommand c;
    c.args << QStringLiteral("disconnect") << endpoint;
    c.timeoutMs = 5000;
    c.label = QStringLiteral("disconnect");
    return run(c, context, std::move(cb));
}

AdbExecutor::Metrics AdbExecutor::metrics() const
{
    QMutexLocker lock(&m_mutex);
    return m_metrics;
}

} // namespace farm
