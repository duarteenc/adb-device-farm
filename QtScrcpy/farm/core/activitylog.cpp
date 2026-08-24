#include "activitylog.h"

#include <algorithm>

#include <QCoreApplication>
#include <QMetaObject>
#include <QThread>

#include "farmlog.h"

namespace farm {

QString ActivityEntry::levelName(Level level)
{
    switch (level) {
    case Info:
        return QStringLiteral("Info");
    case Warning:
        return QStringLiteral("Warning");
    case Error:
        return QStringLiteral("Error");
    }
    return QStringLiteral("Info");
}

QString ActivityEntry::categoryName(Category category)
{
    switch (category) {
    case System:
        return QStringLiteral("System");
    case Device:
        return QStringLiteral("Device");
    case Automation:
        return QStringLiteral("Automation");
    case Adb:
        return QStringLiteral("ADB");
    case Network:
        return QStringLiteral("Network");
    case Power:
        return QStringLiteral("Power");
    case App:
        return QStringLiteral("App");
    }
    return QStringLiteral("System");
}

ActivityLog &ActivityLog::instance()
{
    static ActivityLog log;
    return log;
}

ActivityLog::ActivityLog(QObject *parent)
    : QObject(parent)
{
    qRegisterMetaType<farm::ActivityEntry>("farm::ActivityEntry");
}

void ActivityLog::post(ActivityEntry::Level level, ActivityEntry::Category category, const QString &message,
                       const QString &device)
{
    ActivityEntry entry;
    entry.time = QDateTime::currentDateTime();
    entry.level = level;
    entry.category = category;
    entry.device = device;
    entry.message = message;

    // Mirror into the file log so support bundles contain the same timeline.
    FarmLog::Level fileLevel = FarmLog::Info;
    if (level == ActivityEntry::Warning) {
        fileLevel = FarmLog::Warning;
    } else if (level == ActivityEntry::Error) {
        fileLevel = FarmLog::Error;
    }
    FarmLog::instance().write(fileLevel, ActivityEntry::categoryName(category).toLower(), device, message);

    if (QThread::currentThread() == thread()) {
        append(entry);
    } else {
        QMetaObject::invokeMethod(this, [this, entry]() { append(entry); }, Qt::QueuedConnection);
    }
}

void ActivityLog::append(const ActivityEntry &entryIn)
{
    ActivityEntry entry = entryIn;
    {
        QMutexLocker lock(&m_mutex);
        entry.id = m_nextId++;
        m_entries.append(entry);
        while (m_entries.size() > m_capacity) {
            m_entries.removeFirst();
        }
    }
    emit entryAdded(entry);
}

QList<ActivityEntry> ActivityLog::entries(int maxCount) const
{
    QMutexLocker lock(&m_mutex);
    if (maxCount < 0 || maxCount >= m_entries.size()) {
        return m_entries;
    }
    return m_entries.mid(m_entries.size() - maxCount);
}

void ActivityLog::setCapacity(int capacity)
{
    QMutexLocker lock(&m_mutex);
    m_capacity = std::max(100, capacity);
    while (m_entries.size() > m_capacity) {
        m_entries.removeFirst();
    }
}

void ActivityLog::clear()
{
    {
        QMutexLocker lock(&m_mutex);
        m_entries.clear();
    }
    emit cleared();
}

} // namespace farm
