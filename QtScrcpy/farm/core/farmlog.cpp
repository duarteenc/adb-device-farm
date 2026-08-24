#include "farmlog.h"

#include <algorithm>

#include <QDateTime>
#include <QDir>
#include <QThread>

namespace farm {

namespace {
const char *levelName(FarmLog::Level level)
{
    switch (level) {
    case FarmLog::Debug:
        return "debug";
    case FarmLog::Info:
        return "info";
    case FarmLog::Warning:
        return "warn";
    case FarmLog::Error:
        return "error";
    }
    return "info";
}

QString threadLabel()
{
    QThread *thread = QThread::currentThread();
    if (thread && !thread->objectName().isEmpty()) {
        return thread->objectName();
    }
    return QStringLiteral("0x%1").arg(reinterpret_cast<quintptr>(thread), 0, 16);
}

const char *kLockFileName = "session.lock";
} // namespace

FarmLog &FarmLog::instance()
{
    static FarmLog log;
    return log;
}

FarmLog::FarmLog(QObject *parent)
    : QObject(parent)
{
}

FarmLog::~FarmLog()
{
    close();
}

QString FarmLog::currentFile() const
{
    return m_directory + QStringLiteral("/adb-device-farm.log");
}

bool FarmLog::open(const QString &directory, qint64 maxBytes, int maxFiles)
{
    QMutexLocker lock(&m_mutex);
    QDir dir(directory);
    if (!dir.exists() && !dir.mkpath(QStringLiteral("."))) {
        return false;
    }
    m_directory = dir.absolutePath();
    m_maxBytes = maxBytes;
    m_maxFiles = std::max(1, maxFiles);

    // Crash detection: a lock file left behind means the last run did not exit cleanly.
    QFile lockFile(m_directory + QLatin1Char('/') + QLatin1String(kLockFileName));
    m_previousCrash = lockFile.exists();
    if (lockFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        lockFile.write(QDateTime::currentDateTime().toString(Qt::ISODate).toUtf8());
        lockFile.close();
    }

    m_file.setFileName(currentFile());
    if (!m_file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        return false;
    }
    if (!m_previousHandler) {
        m_previousHandler = qInstallMessageHandler(&FarmLog::messageHandler);
    }
    return true;
}

void FarmLog::close()
{
    QMutexLocker lock(&m_mutex);
    if (m_file.isOpen()) {
        m_file.close();
    }
    if (m_previousHandler) {
        qInstallMessageHandler(m_previousHandler);
        m_previousHandler = nullptr;
    }
}

void FarmLog::markCleanShutdown()
{
    QMutexLocker lock(&m_mutex);
    if (!m_directory.isEmpty()) {
        QFile::remove(m_directory + QLatin1Char('/') + QLatin1String(kLockFileName));
    }
}

void FarmLog::write(Level level, const QString &component, const QString &device, const QString &message)
{
    if (level < m_minLevel) {
        return;
    }
    const QString line = QStringLiteral("%1 [%2] [%3] [%4] [%5] %6\n")
                             .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz")),
                                  threadLabel(), QLatin1String(levelName(level)),
                                  component.isEmpty() ? QStringLiteral("app") : component,
                                  device.isEmpty() ? QStringLiteral("-") : device, message);
    QMutexLocker lock(&m_mutex);
    if (!m_file.isOpen()) {
        return;
    }
    m_file.write(line.toUtf8());
    m_file.flush();
    rotateIfNeeded();
}

void FarmLog::rotateIfNeeded()
{
    // m_mutex is held by the caller.
    if (m_file.size() < m_maxBytes) {
        return;
    }
    m_file.close();
    const QString base = m_directory + QStringLiteral("/adb-device-farm");
    QFile::remove(base + QStringLiteral(".%1.log").arg(m_maxFiles - 1));
    for (int i = m_maxFiles - 2; i >= 1; --i) {
        QFile::rename(base + QStringLiteral(".%1.log").arg(i), base + QStringLiteral(".%1.log").arg(i + 1));
    }
    QFile::rename(base + QStringLiteral(".log"), base + QStringLiteral(".1.log"));
    if (!m_file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        return;    // logging degrades to the previous handler only
    }
}

void FarmLog::messageHandler(QtMsgType type, const QMessageLogContext &context, const QString &msg)
{
    FarmLog &log = instance();
    Level level = Info;
    switch (type) {
    case QtDebugMsg:
        level = Debug;
        break;
    case QtInfoMsg:
        level = Info;
        break;
    case QtWarningMsg:
        level = Warning;
        break;
    case QtCriticalMsg:
    case QtFatalMsg:
        level = Error;
        break;
    }
    log.write(level, context.category ? QString::fromLatin1(context.category) : QStringLiteral("qt"), QString(), msg);
    if (log.m_previousHandler) {
        log.m_previousHandler(type, context, msg);
    }
}

} // namespace farm
