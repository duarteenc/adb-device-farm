#ifndef FARM_CORE_ACTIVITYLOG_H
#define FARM_CORE_ACTIVITYLOG_H

#include <QDateTime>
#include <QList>
#include <QMutex>
#include <QObject>
#include <QString>

namespace farm {

struct ActivityEntry
{
    enum Level { Info, Warning, Error };
    enum Category { System, Device, Automation, Adb, Network, Power, App };

    qint64 id = 0;
    QDateTime time;
    Level level = Info;
    Category category = System;
    QString device;    // device id (serial / ip:port) or empty
    QString message;

    static QString levelName(Level level);
    static QString categoryName(Category category);
};

/**
 * The Activity Center feed: an in-memory ring buffer of user-meaningful events
 * ("Device 12 connected", "APK install 19/20 ok") that the UI filters by level,
 * category and device. Thread-safe; entries posted from worker threads are
 * re-emitted on the ActivityLog's own thread (the GUI thread).
 */
class ActivityLog : public QObject
{
    Q_OBJECT
public:
    static ActivityLog &instance();

    void post(ActivityEntry::Level level, ActivityEntry::Category category, const QString &message,
              const QString &device = QString());
    void info(ActivityEntry::Category category, const QString &message, const QString &device = QString())
    {
        post(ActivityEntry::Info, category, message, device);
    }
    void warning(ActivityEntry::Category category, const QString &message, const QString &device = QString())
    {
        post(ActivityEntry::Warning, category, message, device);
    }
    void error(ActivityEntry::Category category, const QString &message, const QString &device = QString())
    {
        post(ActivityEntry::Error, category, message, device);
    }

    QList<ActivityEntry> entries(int maxCount = -1) const;
    void setCapacity(int capacity);
    void clear();

signals:
    void entryAdded(const farm::ActivityEntry &entry);
    void cleared();

private:
    explicit ActivityLog(QObject *parent = nullptr);
    void append(const ActivityEntry &entry);

    mutable QMutex m_mutex;
    QList<ActivityEntry> m_entries;
    int m_capacity = 5000;
    qint64 m_nextId = 1;
};

} // namespace farm

Q_DECLARE_METATYPE(farm::ActivityEntry)

#endif // FARM_CORE_ACTIVITYLOG_H
