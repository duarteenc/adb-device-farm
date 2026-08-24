#include "repositories.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>

#include "../core/farmlog.h"
#include "database.h"

namespace farm {

namespace {
QSqlDatabase db()
{
    return Database::instance().connection();
}

void logSqlError(const char *where, const QSqlQuery &q)
{
    FarmLog::instance().error(QStringLiteral("storage"), QStringLiteral("%1: %2").arg(QLatin1String(where), q.lastError().text()));
}

QString mapToJson(const QVariantMap &map)
{
    return QString::fromUtf8(QJsonDocument(QJsonObject::fromVariantMap(map)).toJson(QJsonDocument::Compact));
}

QVariantMap jsonToMap(const QString &json)
{
    if (json.isEmpty()) {
        return QVariantMap();
    }
    return QJsonDocument::fromJson(json.toUtf8()).object().toVariantMap();
}

qint64 toEpoch(const QDateTime &dt)
{
    return dt.isValid() ? dt.toSecsSinceEpoch() : 0;
}

QDateTime fromEpoch(const QVariant &v)
{
    const qint64 s = v.toLongLong();
    return s > 0 ? QDateTime::fromSecsSinceEpoch(s) : QDateTime();
}
} // namespace

// ---------------------------------------------------------------- devices

QList<DeviceRecord> DeviceRepository::loadAll()
{
    QList<DeviceRecord> list;
    QSqlDatabase d = db();
    if (!d.isOpen()) {
        return list;
    }
    QSqlQuery q(d);
    if (!q.exec(QStringLiteral("SELECT id, hw_serial, last_ip, port, model, manufacturer, android_version, sdk, friendly_name, number, "
                               "group_name, connection_type, first_seen, last_seen, favorite, notes, bitrate, fps, max_size, keep_awake, "
                               "auto_connect, auto_mirror, props, preset, pinned_order FROM devices"))) {
        logSqlError("DeviceRepository::loadAll", q);
        return list;
    }
    while (q.next()) {
        DeviceRecord r;
        int i = 0;
        r.id = q.value(i++).toString();
        r.hwSerial = q.value(i++).toString();
        r.lastIp = q.value(i++).toString();
        r.port = q.value(i++).toInt();
        r.model = q.value(i++).toString();
        r.manufacturer = q.value(i++).toString();
        r.androidVersion = q.value(i++).toString();
        r.sdk = q.value(i++).toInt();
        r.friendlyName = q.value(i++).toString();
        r.number = q.value(i++).toInt();
        r.group = q.value(i++).toString();
        r.connectionType = static_cast<ConnectionType>(q.value(i++).toInt());
        r.firstSeen = fromEpoch(q.value(i++));
        r.lastSeen = fromEpoch(q.value(i++));
        r.favorite = q.value(i++).toBool();
        r.notes = q.value(i++).toString();
        r.bitRate = q.value(i++).toInt();
        r.fps = q.value(i++).toInt();
        r.maxSize = q.value(i++).toInt();
        r.keepAwake = q.value(i++).toInt();
        r.autoConnect = q.value(i++).toBool();
        r.autoMirror = q.value(i++).toBool();
        r.props = jsonToMap(q.value(i++).toString());
        r.preset = q.value(i++).toString();
        r.pinnedOrder = q.value(i++).toInt();
        r.state = DeviceState::Offline;    // known but not yet seen this session
        list.append(r);
    }
    return list;
}

bool DeviceRepository::save(const DeviceRecord &r)
{
    QSqlDatabase d = db();
    if (!d.isOpen()) {
        return false;
    }
    QSqlQuery q(d);
    q.prepare(QStringLiteral(
        "INSERT INTO devices(id, hw_serial, last_ip, port, model, manufacturer, android_version, sdk, friendly_name, number, group_name, "
        "connection_type, first_seen, last_seen, favorite, notes, bitrate, fps, max_size, keep_awake, auto_connect, auto_mirror, props, preset, pinned_order) "
        "VALUES(:id,:hw,:ip,:port,:model,:manu,:av,:sdk,:fn,:num,:grp,:ct,:fs,:ls,:fav,:notes,:br,:fps,:ms,:ka,:ac,:am,:props,:preset,:po) "
        "ON CONFLICT(id) DO UPDATE SET hw_serial=excluded.hw_serial, last_ip=excluded.last_ip, port=excluded.port, model=excluded.model, "
        "manufacturer=excluded.manufacturer, android_version=excluded.android_version, sdk=excluded.sdk, friendly_name=excluded.friendly_name, "
        "number=excluded.number, group_name=excluded.group_name, connection_type=excluded.connection_type, first_seen=excluded.first_seen, "
        "last_seen=excluded.last_seen, favorite=excluded.favorite, notes=excluded.notes, bitrate=excluded.bitrate, fps=excluded.fps, "
        "max_size=excluded.max_size, keep_awake=excluded.keep_awake, auto_connect=excluded.auto_connect, auto_mirror=excluded.auto_mirror, "
        "props=excluded.props, preset=excluded.preset, pinned_order=excluded.pinned_order"));
    q.bindValue(QStringLiteral(":id"), r.id);
    q.bindValue(QStringLiteral(":hw"), r.hwSerial);
    q.bindValue(QStringLiteral(":ip"), r.lastIp);
    q.bindValue(QStringLiteral(":port"), r.port);
    q.bindValue(QStringLiteral(":model"), r.model);
    q.bindValue(QStringLiteral(":manu"), r.manufacturer);
    q.bindValue(QStringLiteral(":av"), r.androidVersion);
    q.bindValue(QStringLiteral(":sdk"), r.sdk);
    q.bindValue(QStringLiteral(":fn"), r.friendlyName);
    q.bindValue(QStringLiteral(":num"), r.number);
    q.bindValue(QStringLiteral(":grp"), r.group);
    q.bindValue(QStringLiteral(":ct"), static_cast<int>(r.connectionType));
    q.bindValue(QStringLiteral(":fs"), toEpoch(r.firstSeen));
    q.bindValue(QStringLiteral(":ls"), toEpoch(r.lastSeen));
    q.bindValue(QStringLiteral(":fav"), r.favorite ? 1 : 0);
    q.bindValue(QStringLiteral(":notes"), r.notes);
    q.bindValue(QStringLiteral(":br"), r.bitRate);
    q.bindValue(QStringLiteral(":fps"), r.fps);
    q.bindValue(QStringLiteral(":ms"), r.maxSize);
    q.bindValue(QStringLiteral(":ka"), r.keepAwake);
    q.bindValue(QStringLiteral(":ac"), r.autoConnect ? 1 : 0);
    q.bindValue(QStringLiteral(":am"), r.autoMirror ? 1 : 0);
    q.bindValue(QStringLiteral(":props"), mapToJson(r.props));
    q.bindValue(QStringLiteral(":preset"), r.preset);
    q.bindValue(QStringLiteral(":po"), r.pinnedOrder);
    if (!q.exec()) {
        logSqlError("DeviceRepository::save", q);
        return false;
    }
    return true;
}

bool DeviceRepository::saveAll(const QList<DeviceRecord> &records)
{
    return Database::instance().transaction([&records](QSqlDatabase &) {
        for (const DeviceRecord &r : records) {
            if (!save(r)) {
                return false;
            }
        }
        return true;
    });
}

bool DeviceRepository::remove(const QString &id)
{
    QSqlQuery q(db());
    q.prepare(QStringLiteral("DELETE FROM devices WHERE id=?"));
    q.addBindValue(id);
    return q.exec();
}

// ---------------------------------------------------------------- groups

QList<GroupInfo> GroupRepository::loadAll()
{
    QList<GroupInfo> list;
    QSqlDatabase d = db();
    if (!d.isOpen()) {
        return list;
    }
    QSqlQuery q(d);
    if (!q.exec(QStringLiteral("SELECT name, color, sort_order, settings FROM groups ORDER BY sort_order, name"))) {
        logSqlError("GroupRepository::loadAll", q);
        return list;
    }
    while (q.next()) {
        GroupInfo g;
        g.name = q.value(0).toString();
        g.color = q.value(1).toString();
        g.order = q.value(2).toInt();
        g.settings = jsonToMap(q.value(3).toString());
        list.append(g);
    }
    return list;
}

bool GroupRepository::save(const GroupInfo &g)
{
    QSqlQuery q(db());
    q.prepare(QStringLiteral("INSERT INTO groups(name, color, sort_order, settings) VALUES(?,?,?,?) "
                             "ON CONFLICT(name) DO UPDATE SET color=excluded.color, sort_order=excluded.sort_order, settings=excluded.settings"));
    q.addBindValue(g.name);
    q.addBindValue(g.color);
    q.addBindValue(g.order);
    q.addBindValue(mapToJson(g.settings));
    if (!q.exec()) {
        logSqlError("GroupRepository::save", q);
        return false;
    }
    return true;
}

bool GroupRepository::rename(const QString &oldName, const QString &newName)
{
    return Database::instance().transaction([&](QSqlDatabase &d) {
        QSqlQuery q(d);
        q.prepare(QStringLiteral("UPDATE groups SET name=? WHERE name=?"));
        q.addBindValue(newName);
        q.addBindValue(oldName);
        if (!q.exec()) {
            return false;
        }
        QSqlQuery m(d);
        m.prepare(QStringLiteral("UPDATE devices SET group_name=? WHERE group_name=?"));
        m.addBindValue(newName);
        m.addBindValue(oldName);
        return m.exec();
    });
}

bool GroupRepository::remove(const QString &name)
{
    return Database::instance().transaction([&](QSqlDatabase &d) {
        QSqlQuery q(d);
        q.prepare(QStringLiteral("DELETE FROM groups WHERE name=?"));
        q.addBindValue(name);
        if (!q.exec()) {
            return false;
        }
        QSqlQuery m(d);
        m.prepare(QStringLiteral("UPDATE devices SET group_name='' WHERE group_name=?"));
        m.addBindValue(name);
        return m.exec();
    });
}

// ---------------------------------------------------------------- saved commands

QList<SavedCommand> CommandRepository::loadAll()
{
    QList<SavedCommand> list;
    QSqlDatabase d = db();
    if (!d.isOpen()) {
        return list;
    }
    QSqlQuery q(d);
    if (!q.exec(QStringLiteral("SELECT id, name, command, category, description, sort_order FROM saved_commands ORDER BY category, sort_order, name"))) {
        logSqlError("CommandRepository::loadAll", q);
        return list;
    }
    while (q.next()) {
        SavedCommand c;
        c.id = q.value(0).toLongLong();
        c.name = q.value(1).toString();
        c.command = q.value(2).toString();
        c.category = q.value(3).toString();
        c.description = q.value(4).toString();
        c.order = q.value(5).toInt();
        list.append(c);
    }
    return list;
}

qint64 CommandRepository::save(SavedCommand c)
{
    QSqlQuery q(db());
    if (c.id > 0) {
        q.prepare(QStringLiteral("UPDATE saved_commands SET name=?, command=?, category=?, description=?, sort_order=? WHERE id=?"));
        q.addBindValue(c.name);
        q.addBindValue(c.command);
        q.addBindValue(c.category);
        q.addBindValue(c.description);
        q.addBindValue(c.order);
        q.addBindValue(c.id);
        if (!q.exec()) {
            logSqlError("CommandRepository::save", q);
            return 0;
        }
        return c.id;
    }
    q.prepare(QStringLiteral("INSERT INTO saved_commands(name, command, category, description, sort_order) VALUES(?,?,?,?,?)"));
    q.addBindValue(c.name);
    q.addBindValue(c.command);
    q.addBindValue(c.category);
    q.addBindValue(c.description);
    q.addBindValue(c.order);
    if (!q.exec()) {
        logSqlError("CommandRepository::save", q);
        return 0;
    }
    return q.lastInsertId().toLongLong();
}

bool CommandRepository::remove(qint64 id)
{
    QSqlQuery q(db());
    q.prepare(QStringLiteral("DELETE FROM saved_commands WHERE id=?"));
    q.addBindValue(id);
    return q.exec();
}

void CommandRepository::seedDefaultsIfEmpty()
{
    if (!loadAll().isEmpty()) {
        return;
    }
    struct Seed
    {
        const char *name;
        const char *cmd;
        const char *cat;
        const char *desc;
    };
    const Seed seeds[] = {
        { "Screen size", "shell wm size", "Display", "Physical and override display size" },
        { "Screen density", "shell wm density", "Display", "Current DPI" },
        { "Reset screen size", "shell wm size reset", "Display", "Remove any resolution override" },
        { "Reset density", "shell wm density reset", "Display", "Remove any density override" },
        { "Battery status", "shell dumpsys battery", "Power", "Level, charging state, temperature" },
        { "Keep awake ON", "shell svc power stayon true", "Power", "Stay on while plugged in" },
        { "Keep awake OFF", "shell svc power stayon false", "Power", "Restore normal sleep" },
        { "Wake screen", "shell input keyevent KEYCODE_WAKEUP", "Power", "Turn the display on" },
        { "WiFi info", "shell dumpsys wifi | grep -E 'mWifiInfo|SSID|RSSI' | head -5", "Network", "SSID and signal" },
        { "IP addresses", "shell ip -o -4 addr", "Network", "All IPv4 addresses" },
        { "WiFi ON", "shell svc wifi enable", "Network", "Enable WiFi" },
        { "WiFi OFF", "shell svc wifi disable", "Network", "Disable WiFi (careful over WiFi ADB!)" },
        { "List packages", "shell pm list packages -3", "Apps", "Third-party packages" },
        { "Running apps", "shell dumpsys activity activities | grep -E 'mResumedActivity|topResumedActivity'", "Apps", "Foreground activity" },
        { "Device properties", "shell getprop", "Diagnostics", "All system properties" },
        { "Android version", "shell getprop ro.build.version.release", "Diagnostics", "OS version" },
        { "Uptime", "shell uptime", "Diagnostics", "Boot time and load" },
        { "Free storage", "shell df -h /data", "Diagnostics", "Data partition usage" },
        { "Reboot", "reboot", "Maintenance", "Restart the device" },
        { "Clear all app caches", "shell pm trim-caches 999G", "Maintenance", "Ask Android to trim caches" },
        { "Animation scale off", "shell settings put global window_animation_scale 0 && settings put global transition_animation_scale 0 && settings put global animator_duration_scale 0", "Display", "Disable UI animations" },
        { "Animation scale normal", "shell settings put global window_animation_scale 1 && settings put global transition_animation_scale 1 && settings put global animator_duration_scale 1", "Display", "Restore UI animations" },
    };
    int order = 0;
    for (const Seed &s : seeds) {
        SavedCommand c;
        c.name = QString::fromUtf8(s.name);
        c.command = QString::fromUtf8(s.cmd);
        c.category = QString::fromUtf8(s.cat);
        c.description = QString::fromUtf8(s.desc);
        c.order = order++;
        save(c);
    }
}

// ---------------------------------------------------------------- text templates

QList<TextTemplate> TemplateRepository::loadAll()
{
    QList<TextTemplate> list;
    QSqlDatabase d = db();
    if (!d.isOpen()) {
        return list;
    }
    QSqlQuery q(d);
    if (!q.exec(QStringLiteral("SELECT id, name, category, content, shortcut FROM text_templates ORDER BY category, name"))) {
        logSqlError("TemplateRepository::loadAll", q);
        return list;
    }
    while (q.next()) {
        TextTemplate t;
        t.id = q.value(0).toLongLong();
        t.name = q.value(1).toString();
        t.category = q.value(2).toString();
        t.content = q.value(3).toString();
        t.shortcut = q.value(4).toString();
        list.append(t);
    }
    return list;
}

qint64 TemplateRepository::save(TextTemplate t)
{
    QSqlQuery q(db());
    if (t.id > 0) {
        q.prepare(QStringLiteral("UPDATE text_templates SET name=?, category=?, content=?, shortcut=? WHERE id=?"));
        q.addBindValue(t.name);
        q.addBindValue(t.category);
        q.addBindValue(t.content);
        q.addBindValue(t.shortcut);
        q.addBindValue(t.id);
        return q.exec() ? t.id : 0;
    }
    q.prepare(QStringLiteral("INSERT INTO text_templates(name, category, content, shortcut) VALUES(?,?,?,?)"));
    q.addBindValue(t.name);
    q.addBindValue(t.category);
    q.addBindValue(t.content);
    q.addBindValue(t.shortcut);
    if (!q.exec()) {
        logSqlError("TemplateRepository::save", q);
        return 0;
    }
    return q.lastInsertId().toLongLong();
}

bool TemplateRepository::remove(qint64 id)
{
    QSqlQuery q(db());
    q.prepare(QStringLiteral("DELETE FROM text_templates WHERE id=?"));
    q.addBindValue(id);
    return q.exec();
}

// ---------------------------------------------------------------- workflows

QList<WorkflowRow> WorkflowRepository::loadAll()
{
    QList<WorkflowRow> list;
    QSqlDatabase d = db();
    if (!d.isOpen()) {
        return list;
    }
    QSqlQuery q(d);
    if (!q.exec(QStringLiteral("SELECT id, name, json, created, updated FROM workflows ORDER BY name"))) {
        logSqlError("WorkflowRepository::loadAll", q);
        return list;
    }
    while (q.next()) {
        WorkflowRow w;
        w.id = q.value(0).toString();
        w.name = q.value(1).toString();
        w.json = q.value(2).toString();
        w.created = fromEpoch(q.value(3));
        w.updated = fromEpoch(q.value(4));
        list.append(w);
    }
    return list;
}

WorkflowRow WorkflowRepository::load(const QString &id)
{
    WorkflowRow w;
    QSqlQuery q(db());
    q.prepare(QStringLiteral("SELECT id, name, json, created, updated FROM workflows WHERE id=?"));
    q.addBindValue(id);
    if (q.exec() && q.next()) {
        w.id = q.value(0).toString();
        w.name = q.value(1).toString();
        w.json = q.value(2).toString();
        w.created = fromEpoch(q.value(3));
        w.updated = fromEpoch(q.value(4));
    }
    return w;
}

bool WorkflowRepository::save(const WorkflowRow &w)
{
    QSqlQuery q(db());
    q.prepare(QStringLiteral("INSERT INTO workflows(id, name, json, created, updated) VALUES(?,?,?,?,?) "
                             "ON CONFLICT(id) DO UPDATE SET name=excluded.name, json=excluded.json, updated=excluded.updated"));
    q.addBindValue(w.id);
    q.addBindValue(w.name);
    q.addBindValue(w.json);
    q.addBindValue(toEpoch(w.created.isValid() ? w.created : QDateTime::currentDateTime()));
    q.addBindValue(toEpoch(w.updated.isValid() ? w.updated : QDateTime::currentDateTime()));
    if (!q.exec()) {
        logSqlError("WorkflowRepository::save", q);
        return false;
    }
    return true;
}

bool WorkflowRepository::remove(const QString &id)
{
    QSqlQuery q(db());
    q.prepare(QStringLiteral("DELETE FROM workflows WHERE id=?"));
    q.addBindValue(id);
    return q.exec();
}

// ---------------------------------------------------------------- schedules

QList<ScheduleRow> ScheduleRepository::loadAll()
{
    QList<ScheduleRow> list;
    QSqlDatabase d = db();
    if (!d.isOpen()) {
        return list;
    }
    QSqlQuery q(d);
    if (!q.exec(QStringLiteral("SELECT id, name, json, enabled, next_run, last_run FROM schedules ORDER BY name"))) {
        logSqlError("ScheduleRepository::loadAll", q);
        return list;
    }
    while (q.next()) {
        ScheduleRow s;
        s.id = q.value(0).toString();
        s.name = q.value(1).toString();
        s.json = q.value(2).toString();
        s.enabled = q.value(3).toBool();
        s.nextRun = fromEpoch(q.value(4));
        s.lastRun = fromEpoch(q.value(5));
        list.append(s);
    }
    return list;
}

bool ScheduleRepository::save(const ScheduleRow &s)
{
    QSqlQuery q(db());
    q.prepare(QStringLiteral("INSERT INTO schedules(id, name, json, enabled, next_run, last_run) VALUES(?,?,?,?,?,?) "
                             "ON CONFLICT(id) DO UPDATE SET name=excluded.name, json=excluded.json, enabled=excluded.enabled, "
                             "next_run=excluded.next_run, last_run=excluded.last_run"));
    q.addBindValue(s.id);
    q.addBindValue(s.name);
    q.addBindValue(s.json);
    q.addBindValue(s.enabled ? 1 : 0);
    q.addBindValue(toEpoch(s.nextRun));
    q.addBindValue(toEpoch(s.lastRun));
    if (!q.exec()) {
        logSqlError("ScheduleRepository::save", q);
        return false;
    }
    return true;
}

bool ScheduleRepository::remove(const QString &id)
{
    QSqlQuery q(db());
    q.prepare(QStringLiteral("DELETE FROM schedules WHERE id=?"));
    q.addBindValue(id);
    return q.exec();
}

// ---------------------------------------------------------------- job runs / logs

QList<JobRunRow> RunRepository::loadRecent(int limit)
{
    QList<JobRunRow> list;
    QSqlDatabase d = db();
    if (!d.isOpen()) {
        return list;
    }
    QSqlQuery q(d);
    q.prepare(QStringLiteral("SELECT id, kind, name, workflow_id, started, finished, status, total, succeeded, failed, json FROM job_runs "
                             "ORDER BY started DESC LIMIT ?"));
    q.addBindValue(limit);
    if (!q.exec()) {
        logSqlError("RunRepository::loadRecent", q);
        return list;
    }
    while (q.next()) {
        JobRunRow r;
        r.id = q.value(0).toString();
        r.kind = q.value(1).toString();
        r.name = q.value(2).toString();
        r.workflowId = q.value(3).toString();
        r.started = fromEpoch(q.value(4));
        r.finished = fromEpoch(q.value(5));
        r.status = q.value(6).toString();
        r.total = q.value(7).toInt();
        r.succeeded = q.value(8).toInt();
        r.failed = q.value(9).toInt();
        r.json = q.value(10).toString();
        list.append(r);
    }
    return list;
}

bool RunRepository::saveRun(const JobRunRow &r)
{
    QSqlQuery q(db());
    q.prepare(QStringLiteral("INSERT INTO job_runs(id, kind, name, workflow_id, started, finished, status, total, succeeded, failed, json) "
                             "VALUES(?,?,?,?,?,?,?,?,?,?,?) ON CONFLICT(id) DO UPDATE SET finished=excluded.finished, status=excluded.status, "
                             "total=excluded.total, succeeded=excluded.succeeded, failed=excluded.failed, json=excluded.json"));
    q.addBindValue(r.id);
    q.addBindValue(r.kind);
    q.addBindValue(r.name);
    q.addBindValue(r.workflowId);
    q.addBindValue(toEpoch(r.started));
    q.addBindValue(toEpoch(r.finished));
    q.addBindValue(r.status);
    q.addBindValue(r.total);
    q.addBindValue(r.succeeded);
    q.addBindValue(r.failed);
    q.addBindValue(r.json);
    if (!q.exec()) {
        logSqlError("RunRepository::saveRun", q);
        return false;
    }
    return true;
}

bool RunRepository::appendLog(const JobLogRow &l)
{
    QSqlQuery q(db());
    q.prepare(QStringLiteral("INSERT INTO job_logs(run_id, device, ts, step, status, duration, message, error, screenshot) VALUES(?,?,?,?,?,?,?,?,?)"));
    q.addBindValue(l.runId);
    q.addBindValue(l.device);
    q.addBindValue(toEpoch(l.time.isValid() ? l.time : QDateTime::currentDateTime()));
    q.addBindValue(l.step);
    q.addBindValue(l.status);
    q.addBindValue(l.durationMs);
    q.addBindValue(l.message);
    q.addBindValue(l.error);
    q.addBindValue(l.screenshot);
    if (!q.exec()) {
        logSqlError("RunRepository::appendLog", q);
        return false;
    }
    return true;
}

bool RunRepository::appendLogs(const QList<JobLogRow> &rows)
{
    return Database::instance().transaction([&rows](QSqlDatabase &) {
        for (const JobLogRow &r : rows) {
            if (!appendLog(r)) {
                return false;
            }
        }
        return true;
    });
}

QList<JobLogRow> RunRepository::loadLogs(const QString &runId, bool failuresOnly)
{
    QList<JobLogRow> list;
    QSqlQuery q(db());
    q.prepare(QStringLiteral("SELECT id, run_id, device, ts, step, status, duration, message, error, screenshot FROM job_logs WHERE run_id=? %1 ORDER BY id")
                  .arg(failuresOnly ? QStringLiteral("AND status='failed'") : QString()));
    q.addBindValue(runId);
    if (!q.exec()) {
        logSqlError("RunRepository::loadLogs", q);
        return list;
    }
    while (q.next()) {
        JobLogRow l;
        l.id = q.value(0).toLongLong();
        l.runId = q.value(1).toString();
        l.device = q.value(2).toString();
        l.time = fromEpoch(q.value(3));
        l.step = q.value(4).toString();
        l.status = q.value(5).toString();
        l.durationMs = q.value(6).toLongLong();
        l.message = q.value(7).toString();
        l.error = q.value(8).toString();
        l.screenshot = q.value(9).toString();
        list.append(l);
    }
    return list;
}

bool RunRepository::removeRun(const QString &runId)
{
    return Database::instance().transaction([&](QSqlDatabase &d) {
        QSqlQuery a(d);
        a.prepare(QStringLiteral("DELETE FROM job_logs WHERE run_id=?"));
        a.addBindValue(runId);
        if (!a.exec()) {
            return false;
        }
        QSqlQuery b(d);
        b.prepare(QStringLiteral("DELETE FROM job_runs WHERE id=?"));
        b.addBindValue(runId);
        return b.exec();
    });
}

int RunRepository::pruneOlderThan(int days)
{
    const qint64 cutoff = QDateTime::currentDateTime().addDays(-days).toSecsSinceEpoch();
    int removed = 0;
    Database::instance().transaction([&](QSqlDatabase &d) {
        QSqlQuery a(d);
        a.prepare(QStringLiteral("DELETE FROM job_logs WHERE run_id IN (SELECT id FROM job_runs WHERE started < ?)"));
        a.addBindValue(cutoff);
        a.exec();
        QSqlQuery b(d);
        b.prepare(QStringLiteral("DELETE FROM job_runs WHERE started < ?"));
        b.addBindValue(cutoff);
        if (b.exec()) {
            removed = b.numRowsAffected();
        }
        return true;
    });
    return removed;
}

// ---------------------------------------------------------------- activity

bool ActivityRepository::append(int level, int category, const QString &device, const QString &message, const QDateTime &time)
{
    QSqlDatabase d = db();
    if (!d.isOpen()) {
        return false;
    }
    QSqlQuery q(d);
    q.prepare(QStringLiteral("INSERT INTO activity(ts, level, category, device, message) VALUES(?,?,?,?,?)"));
    q.addBindValue(toEpoch(time));
    q.addBindValue(level);
    q.addBindValue(category);
    q.addBindValue(device);
    q.addBindValue(message);
    return q.exec();
}

int ActivityRepository::pruneOlderThan(int days)
{
    QSqlQuery q(db());
    q.prepare(QStringLiteral("DELETE FROM activity WHERE ts < ?"));
    q.addBindValue(QDateTime::currentDateTime().addDays(-days).toSecsSinceEpoch());
    return q.exec() ? q.numRowsAffected() : 0;
}

// ---------------------------------------------------------------- kv

QString KvRepository::get(const QString &key, const QString &fallback)
{
    QSqlDatabase d = db();
    if (!d.isOpen()) {
        return fallback;
    }
    QSqlQuery q(d);
    q.prepare(QStringLiteral("SELECT value FROM kv WHERE key=?"));
    q.addBindValue(key);
    if (q.exec() && q.next()) {
        return q.value(0).toString();
    }
    return fallback;
}

bool KvRepository::set(const QString &key, const QString &value)
{
    QSqlDatabase d = db();
    if (!d.isOpen()) {
        return false;
    }
    QSqlQuery q(d);
    q.prepare(QStringLiteral("INSERT INTO kv(key, value) VALUES(?,?) ON CONFLICT(key) DO UPDATE SET value=excluded.value"));
    q.addBindValue(key);
    q.addBindValue(value);
    return q.exec();
}

} // namespace farm
