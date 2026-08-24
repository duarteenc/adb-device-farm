#ifndef FARM_SCHEDULER_SCHEDULER_H
#define FARM_SCHEDULER_SCHEDULER_H

#include <QDateTime>
#include <QHash>
#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QStringList>
#include <QTime>
#include <QTimer>

namespace farm {

struct Schedule
{
    QString id;
    QString name;
    QString workflowId;          // id or name
    QString targetsMode = QStringLiteral("all");    // selection | group | all | online | devices
    QString group;
    QStringList devices;
    int concurrency = 5;
    bool enabled = true;

    // kind: once | interval | hourly | daily | weekly | weekdays | event | appStart
    QString kind = QStringLiteral("daily");
    QDateTime onceAt;
    int intervalMinutes = 60;
    QTime timeOfDay = QTime(9, 0);
    QList<int> weekdays;         // 1 = Monday … 7 = Sunday (for weekly/weekdays)
    // events: deviceConnected | deviceDisconnected | deviceReconnected | batteryBelow | batteryAbove | temperatureAbove | appStart
    QString eventType;
    double eventThreshold = 20;
    int eventCooldownSeconds = 300;    // per device
    QString missedPolicy = QStringLiteral("skip");    // skip | immediate | nextStart

    QDateTime lastRun;
    QDateTime nextRun;
    QString lastResult;

    QJsonObject toJson() const;
    static Schedule fromJson(const QJsonObject &o);
    QString describe() const;
};

/**
 * Persistent scheduler: time-based schedules (once / every N minutes / hourly /
 * daily / weekly / weekdays at a time) and event triggers (device connected,
 * reconnected, battery, temperature, app start). Missed runs follow the
 * per-schedule policy (skip / run immediately / run once at next start), and a
 * schedule never launches a second run while its previous run is still active.
 */
class Scheduler : public QObject
{
    Q_OBJECT
public:
    static Scheduler &instance();

    void start();
    void stop();
    QList<Schedule> schedules() const { return m_schedules; }
    Schedule schedule(const QString &id) const;
    void save(Schedule schedule);
    void remove(const QString &id);
    void runNow(const QString &id, const QString &reason = QStringLiteral("manual"));
    void setEnabled(const QString &id, bool enabled);

    /// Pure: next fire time strictly after `from` (invalid for event/once-in-the-past).
    static QDateTime computeNextRun(const Schedule &s, const QDateTime &from);

signals:
    void schedulesChanged();
    void scheduleFired(const QString &scheduleId, const QString &runId);

private:
    explicit Scheduler(QObject *parent = nullptr);
    void load();
    void tick();
    void fire(Schedule &s, const QString &reason, const QStringList &deviceOverride = QStringList());
    void onDeviceEvent(const QString &eventType, const QString &deviceId, double value);
    void persist(const Schedule &s);

    QTimer m_timer;
    QList<Schedule> m_schedules;
    QHash<QString, QString> m_activeRuns;            // schedule id -> run id
    QHash<QString, QDateTime> m_eventCooldown;       // schedule|device -> last fire
    bool m_running = false;
};

} // namespace farm

#endif // FARM_SCHEDULER_SCHEDULER_H
