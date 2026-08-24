#include "scheduler.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QUuid>

#include "../automation/workflowengine.h"
#include "../core/activitylog.h"
#include "../core/farmlog.h"
#include "../core/farmsettings.h"
#include "../devices/devicehealthmonitor.h"
#include "../devices/deviceregistry.h"
#include "../devices/deviceservice.h"
#include "../storage/repositories.h"

namespace farm {

// ---------------------------------------------------------------- Schedule

QJsonObject Schedule::toJson() const
{
    QJsonObject o;
    o[QStringLiteral("id")] = id;
    o[QStringLiteral("name")] = name;
    o[QStringLiteral("workflowId")] = workflowId;
    o[QStringLiteral("targetsMode")] = targetsMode;
    o[QStringLiteral("group")] = group;
    o[QStringLiteral("devices")] = QJsonArray::fromStringList(devices);
    o[QStringLiteral("concurrency")] = concurrency;
    o[QStringLiteral("enabled")] = enabled;
    o[QStringLiteral("kind")] = kind;
    o[QStringLiteral("onceAt")] = onceAt.isValid() ? onceAt.toString(Qt::ISODate) : QString();
    o[QStringLiteral("intervalMinutes")] = intervalMinutes;
    o[QStringLiteral("timeOfDay")] = timeOfDay.toString(QStringLiteral("HH:mm"));
    QJsonArray wd;
    for (int d : weekdays) {
        wd.append(d);
    }
    o[QStringLiteral("weekdays")] = wd;
    o[QStringLiteral("eventType")] = eventType;
    o[QStringLiteral("eventThreshold")] = eventThreshold;
    o[QStringLiteral("eventCooldownSeconds")] = eventCooldownSeconds;
    o[QStringLiteral("missedPolicy")] = missedPolicy;
    o[QStringLiteral("lastRun")] = lastRun.isValid() ? lastRun.toString(Qt::ISODate) : QString();
    o[QStringLiteral("nextRun")] = nextRun.isValid() ? nextRun.toString(Qt::ISODate) : QString();
    o[QStringLiteral("lastResult")] = lastResult;
    return o;
}

Schedule Schedule::fromJson(const QJsonObject &o)
{
    Schedule s;
    s.id = o.value(QStringLiteral("id")).toString();
    s.name = o.value(QStringLiteral("name")).toString();
    s.workflowId = o.value(QStringLiteral("workflowId")).toString();
    s.targetsMode = o.value(QStringLiteral("targetsMode")).toString(QStringLiteral("all"));
    s.group = o.value(QStringLiteral("group")).toString();
    for (const QJsonValue &v : o.value(QStringLiteral("devices")).toArray()) {
        s.devices << v.toString();
    }
    s.concurrency = o.value(QStringLiteral("concurrency")).toInt(5);
    s.enabled = o.value(QStringLiteral("enabled")).toBool(true);
    s.kind = o.value(QStringLiteral("kind")).toString(QStringLiteral("daily"));
    s.onceAt = QDateTime::fromString(o.value(QStringLiteral("onceAt")).toString(), Qt::ISODate);
    s.intervalMinutes = o.value(QStringLiteral("intervalMinutes")).toInt(60);
    s.timeOfDay = QTime::fromString(o.value(QStringLiteral("timeOfDay")).toString(QStringLiteral("09:00")), QStringLiteral("HH:mm"));
    for (const QJsonValue &v : o.value(QStringLiteral("weekdays")).toArray()) {
        s.weekdays << v.toInt();
    }
    s.eventType = o.value(QStringLiteral("eventType")).toString();
    s.eventThreshold = o.value(QStringLiteral("eventThreshold")).toDouble(20);
    s.eventCooldownSeconds = o.value(QStringLiteral("eventCooldownSeconds")).toInt(300);
    s.missedPolicy = o.value(QStringLiteral("missedPolicy")).toString(QStringLiteral("skip"));
    s.lastRun = QDateTime::fromString(o.value(QStringLiteral("lastRun")).toString(), Qt::ISODate);
    s.nextRun = QDateTime::fromString(o.value(QStringLiteral("nextRun")).toString(), Qt::ISODate);
    s.lastResult = o.value(QStringLiteral("lastResult")).toString();
    return s;
}

QString Schedule::describe() const
{
    if (kind == QLatin1String("once")) {
        return QStringLiteral("Once at %1").arg(onceAt.toString(QStringLiteral("yyyy-MM-dd HH:mm")));
    }
    if (kind == QLatin1String("interval")) {
        return QStringLiteral("Every %1 min").arg(intervalMinutes);
    }
    if (kind == QLatin1String("hourly")) {
        return QStringLiteral("Hourly at :%1").arg(timeOfDay.minute(), 2, 10, QLatin1Char('0'));
    }
    if (kind == QLatin1String("daily")) {
        return QStringLiteral("Daily at %1").arg(timeOfDay.toString(QStringLiteral("HH:mm")));
    }
    if (kind == QLatin1String("weekly") || kind == QLatin1String("weekdays")) {
        static const char *names[] = { "", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun" };
        QStringList d;
        for (int w : weekdays) {
            if (w >= 1 && w <= 7) {
                d << QLatin1String(names[w]);
            }
        }
        return QStringLiteral("%1 at %2").arg(d.isEmpty() ? QStringLiteral("(no days)") : d.join(QLatin1Char(',')), timeOfDay.toString(QStringLiteral("HH:mm")));
    }
    if (kind == QLatin1String("appStart")) {
        return QStringLiteral("On application start");
    }
    if (kind == QLatin1String("event")) {
        return QStringLiteral("On %1%2").arg(eventType, (eventType.startsWith(QLatin1String("battery")) || eventType.startsWith(QLatin1String("temperature"))) ? QStringLiteral(" %1").arg(eventThreshold) : QString());
    }
    return kind;
}

// ---------------------------------------------------------------- next-run computation

QDateTime Scheduler::computeNextRun(const Schedule &s, const QDateTime &from)
{
    if (s.kind == QLatin1String("once")) {
        return s.onceAt.isValid() && s.onceAt > from ? s.onceAt : QDateTime();
    }
    if (s.kind == QLatin1String("interval")) {
        const int minutes = std::max(1, s.intervalMinutes);
        QDateTime base = s.lastRun.isValid() ? s.lastRun : from;
        QDateTime next = base.addSecs(60 * minutes);
        while (next <= from) {
            next = next.addSecs(60 * minutes);
        }
        return next;
    }
    if (s.kind == QLatin1String("hourly")) {
        QDateTime next(from.date(), QTime(from.time().hour(), s.timeOfDay.minute()));
        while (next <= from) {
            next = next.addSecs(3600);
        }
        return next;
    }
    if (s.kind == QLatin1String("daily")) {
        QDateTime next(from.date(), s.timeOfDay);
        while (next <= from) {
            next = next.addDays(1);
        }
        return next;
    }
    if (s.kind == QLatin1String("weekly") || s.kind == QLatin1String("weekdays")) {
        if (s.weekdays.isEmpty()) {
            return QDateTime();
        }
        QDateTime next(from.date(), s.timeOfDay);
        for (int i = 0; i < 8; ++i) {
            if (next > from && s.weekdays.contains(next.date().dayOfWeek())) {
                return next;
            }
            next = next.addDays(1);
        }
        return QDateTime();
    }
    return QDateTime();    // event / appStart
}

// ---------------------------------------------------------------- Scheduler

Scheduler &Scheduler::instance()
{
    static Scheduler scheduler;
    return scheduler;
}

Scheduler::Scheduler(QObject *parent)
    : QObject(parent)
{
    connect(&m_timer, &QTimer::timeout, this, &Scheduler::tick);
}

void Scheduler::load()
{
    m_schedules.clear();
    for (const ScheduleRow &row : ScheduleRepository::loadAll()) {
        Schedule s = Schedule::fromJson(QJsonDocument::fromJson(row.json.toUtf8()).object());
        if (s.id.isEmpty()) {
            s.id = row.id;
        }
        s.enabled = row.enabled;
        if (!s.lastRun.isValid()) {
            s.lastRun = row.lastRun;
        }
        m_schedules.append(s);
    }
}

void Scheduler::persist(const Schedule &s)
{
    ScheduleRow row;
    row.id = s.id;
    row.name = s.name;
    row.json = QString::fromUtf8(QJsonDocument(s.toJson()).toJson(QJsonDocument::Compact));
    row.enabled = s.enabled;
    row.nextRun = s.nextRun;
    row.lastRun = s.lastRun;
    ScheduleRepository::save(row);
}

void Scheduler::start()
{
    if (m_running) {
        return;
    }
    m_running = true;
    load();
    const QDateTime now = QDateTime::currentDateTime();
    // Startup: resolve missed runs per policy, compute next runs, fire appStart schedules.
    for (Schedule &s : m_schedules) {
        if (!s.enabled) {
            continue;
        }
        if (s.kind == QLatin1String("appStart")) {
            fire(s, QStringLiteral("application start"));
            continue;
        }
        if (s.kind == QLatin1String("event")) {
            continue;
        }
        if (s.nextRun.isValid() && s.nextRun < now) {
            if (s.missedPolicy == QLatin1String("immediate") || s.missedPolicy == QLatin1String("nextStart")) {
                FarmLog::instance().info(QStringLiteral("scheduler"), QStringLiteral("'%1' missed at %2 — running now (%3)").arg(s.name, s.nextRun.toString(Qt::ISODate), s.missedPolicy));
                fire(s, QStringLiteral("missed run (%1)").arg(s.missedPolicy));
            } else {
                FarmLog::instance().info(QStringLiteral("scheduler"), QStringLiteral("'%1' missed at %2 — skipped").arg(s.name, s.nextRun.toString(Qt::ISODate)));
            }
        }
        s.nextRun = computeNextRun(s, now);
        persist(s);
    }
    // Event sources
    connect(&DeviceRegistry::instance(), &DeviceRegistry::stateChanged, this, [this](const QString &id, DeviceState oldState, DeviceState newState) {
        const bool wasOnline = deviceStateIsOnline(oldState);
        const bool isOnline = deviceStateIsOnline(newState);
        if (!wasOnline && isOnline) {
            onDeviceEvent(oldState == DeviceState::Reconnecting ? QStringLiteral("deviceReconnected") : QStringLiteral("deviceConnected"), id, 0);
            if (oldState == DeviceState::Reconnecting) {
                onDeviceEvent(QStringLiteral("deviceConnected"), id, 0);
            }
        } else if (wasOnline && !isOnline) {
            onDeviceEvent(QStringLiteral("deviceDisconnected"), id, 0);
        }
    });
    connect(&DeviceHealthMonitor::instance(), &DeviceHealthMonitor::batteryBelow, this, [this](const QString &id, int level) { onDeviceEvent(QStringLiteral("batteryBelow"), id, level); });
    connect(&DeviceHealthMonitor::instance(), &DeviceHealthMonitor::batteryAbove, this, [this](const QString &id, int level) { onDeviceEvent(QStringLiteral("batteryAbove"), id, level); });
    connect(&DeviceHealthMonitor::instance(), &DeviceHealthMonitor::temperatureAbove, this, [this](const QString &id, double c) { onDeviceEvent(QStringLiteral("temperatureAbove"), id, c); });
    m_timer.start(std::clamp(FarmSettings::instance().intValue(QStringLiteral("scheduler/tickSeconds"), 30), 5, 600) * 1000);
    emit schedulesChanged();
}

void Scheduler::stop()
{
    m_running = false;
    m_timer.stop();
}

Schedule Scheduler::schedule(const QString &id) const
{
    for (const Schedule &s : m_schedules) {
        if (s.id == id) {
            return s;
        }
    }
    return Schedule();
}

void Scheduler::save(Schedule s)
{
    if (s.id.isEmpty()) {
        s.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    }
    if (s.kind != QLatin1String("event") && s.kind != QLatin1String("appStart")) {
        s.nextRun = computeNextRun(s, QDateTime::currentDateTime());
    } else {
        s.nextRun = QDateTime();
    }
    bool replaced = false;
    for (Schedule &e : m_schedules) {
        if (e.id == s.id) {
            e = s;
            replaced = true;
        }
    }
    if (!replaced) {
        m_schedules.append(s);
    }
    persist(s);
    emit schedulesChanged();
}

void Scheduler::remove(const QString &id)
{
    for (int i = 0; i < m_schedules.size(); ++i) {
        if (m_schedules.at(i).id == id) {
            m_schedules.removeAt(i);
            break;
        }
    }
    ScheduleRepository::remove(id);
    emit schedulesChanged();
}

void Scheduler::setEnabled(const QString &id, bool enabled)
{
    for (Schedule &s : m_schedules) {
        if (s.id == id) {
            s.enabled = enabled;
            if (enabled) {
                s.nextRun = computeNextRun(s, QDateTime::currentDateTime());
            }
            persist(s);
        }
    }
    emit schedulesChanged();
}

void Scheduler::runNow(const QString &id, const QString &reason)
{
    for (Schedule &s : m_schedules) {
        if (s.id == id) {
            fire(s, reason);
        }
    }
}

void Scheduler::tick()
{
    if (!FarmSettings::instance().boolValue(QStringLiteral("scheduler/enabled"), true)) {
        return;
    }
    const QDateTime now = QDateTime::currentDateTime();
    for (Schedule &s : m_schedules) {
        if (!s.enabled || s.kind == QLatin1String("event") || s.kind == QLatin1String("appStart") || !s.nextRun.isValid()) {
            continue;
        }
        if (s.nextRun <= now) {
            // Catch-up: a sleeping PC may have missed several intervals; run once per policy.
            if (now.secsTo(s.nextRun) < -3600 && s.missedPolicy == QLatin1String("skip")) {
                FarmLog::instance().info(QStringLiteral("scheduler"), QStringLiteral("'%1' missed at %2 — skipped").arg(s.name, s.nextRun.toString(Qt::ISODate)));
            } else {
                fire(s, QStringLiteral("scheduled %1").arg(s.describe()));
            }
            s.nextRun = computeNextRun(s, now);
            if (s.kind == QLatin1String("once")) {
                s.enabled = false;
            }
            persist(s);
            emit schedulesChanged();
        }
    }
}

void Scheduler::onDeviceEvent(const QString &eventType, const QString &deviceId, double value)
{
    if (!FarmSettings::instance().boolValue(QStringLiteral("scheduler/enabled"), true)) {
        return;
    }
    for (Schedule &s : m_schedules) {
        if (!s.enabled || s.kind != QLatin1String("event") || s.eventType != eventType) {
            continue;
        }
        if ((eventType == QLatin1String("batteryBelow") && value > s.eventThreshold) || (eventType == QLatin1String("batteryAbove") && value < s.eventThreshold)
            || (eventType == QLatin1String("temperatureAbove") && value < s.eventThreshold)) {
            continue;
        }
        const QString key = s.id + QLatin1Char('|') + deviceId;
        const QDateTime last = m_eventCooldown.value(key);
        if (last.isValid() && last.secsTo(QDateTime::currentDateTime()) < s.eventCooldownSeconds) {
            continue;
        }
        m_eventCooldown.insert(key, QDateTime::currentDateTime());
        // Event schedules run on the device that triggered them (unless targeting a group/all explicitly).
        const QStringList override = (s.targetsMode == QLatin1String("all") || s.targetsMode == QLatin1String("selection") || s.targetsMode == QLatin1String("devices")) ? QStringList{ deviceId } : QStringList();
        fire(s, QStringLiteral("event %1 on %2").arg(eventType, deviceId), override);
    }
}

void Scheduler::fire(Schedule &s, const QString &reason, const QStringList &deviceOverride)
{
    // Never stack runs of the same schedule.
    if (m_activeRuns.contains(s.id)) {
        if (AutomationRun *active = WorkflowEngine::instance().run(m_activeRuns.value(s.id))) {
            if (active->status() == AutomationRun::Running || active->status() == AutomationRun::Paused) {
                FarmLog::instance().info(QStringLiteral("scheduler"), QStringLiteral("'%1' skipped: previous run still active").arg(s.name));
                s.lastResult = QStringLiteral("skipped (previous run active)");
                return;
            }
        }
        m_activeRuns.remove(s.id);
    }
    bool found = false;
    const Workflow wf = WorkflowEngine::loadWorkflow(s.workflowId, &found);
    if (!found) {
        s.lastResult = QStringLiteral("workflow '%1' not found").arg(s.workflowId);
        ActivityLog::instance().error(ActivityEntry::Automation, QStringLiteral("Schedule '%1': %2").arg(s.name, s.lastResult));
        return;
    }
    QStringList targets = deviceOverride.isEmpty() ? WorkflowEngine::resolveTargets(s.targetsMode, s.group, s.devices) : deviceOverride;
    if (targets.isEmpty()) {
        s.lastResult = QStringLiteral("no online targets");
        FarmLog::instance().info(QStringLiteral("scheduler"), QStringLiteral("'%1': no online targets").arg(s.name));
        return;
    }
    AutomationRun *run = WorkflowEngine::instance().start(wf, targets, s.concurrency, QStringLiteral("schedule '%1' — %2").arg(s.name, reason));
    m_activeRuns.insert(s.id, run->id());
    s.lastRun = QDateTime::currentDateTime();
    s.lastResult = QStringLiteral("started on %1 device(s)").arg(targets.size());
    persist(s);
    emit scheduleFired(s.id, run->id());
    ActivityLog::instance().info(ActivityEntry::Automation, QStringLiteral("Schedule '%1' fired (%2): %3 device(s)").arg(s.name, reason).arg(targets.size()));
}

} // namespace farm
