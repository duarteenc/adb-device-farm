#ifndef FARM_STORAGE_REPOSITORIES_H
#define FARM_STORAGE_REPOSITORIES_H

#include <QDateTime>
#include <QList>
#include <QString>
#include <QStringList>
#include <QVariantMap>

#include "../devices/devicerecord.h"

namespace farm {

/// devices table
class DeviceRepository
{
public:
    static QList<DeviceRecord> loadAll();
    static bool save(const DeviceRecord &record);
    static bool saveAll(const QList<DeviceRecord> &records);
    static bool remove(const QString &id);
};

struct GroupInfo
{
    QString name;
    QString color;      // "#3b82f6"
    int order = 0;
    QVariantMap settings;    // keepAwake (-1/0/1), preset, bitRate, fps, maxSize
};

/// groups table
class GroupRepository
{
public:
    static QList<GroupInfo> loadAll();
    static bool save(const GroupInfo &group);
    static bool rename(const QString &oldName, const QString &newName);
    static bool remove(const QString &name);
};

struct SavedCommand
{
    qint64 id = 0;
    QString name;
    QString command;      // without the leading "adb"; may start with "shell"
    QString category;     // Display / Power / Network / Apps / Diagnostics / Maintenance
    QString description;
    int order = 0;
};

/// saved_commands table
class CommandRepository
{
public:
    static QList<SavedCommand> loadAll();
    static qint64 save(SavedCommand command);    // insert or update; returns id
    static bool remove(qint64 id);
    static void seedDefaultsIfEmpty();
};

struct TextTemplate
{
    qint64 id = 0;
    QString name;
    QString category;
    QString content;
    QString shortcut;
};

/// text_templates table
class TemplateRepository
{
public:
    static QList<TextTemplate> loadAll();
    static qint64 save(TextTemplate tpl);
    static bool remove(qint64 id);
};

struct WorkflowRow
{
    QString id;
    QString name;
    QString json;
    QDateTime created;
    QDateTime updated;
};

/// workflows table
class WorkflowRepository
{
public:
    static QList<WorkflowRow> loadAll();
    static WorkflowRow load(const QString &id);
    static bool save(const WorkflowRow &row);
    static bool remove(const QString &id);
};

struct ScheduleRow
{
    QString id;
    QString name;
    QString json;
    bool enabled = true;
    QDateTime nextRun;
    QDateTime lastRun;
};

/// schedules table
class ScheduleRepository
{
public:
    static QList<ScheduleRow> loadAll();
    static bool save(const ScheduleRow &row);
    static bool remove(const QString &id);
};

struct JobRunRow
{
    QString id;
    QString kind;        // workflow / batch
    QString name;
    QString workflowId;
    QDateTime started;
    QDateTime finished;
    QString status;      // Pending Running Paused Completed Failed Cancelled
    int total = 0;
    int succeeded = 0;
    int failed = 0;
    QString json;        // targets, options
};

struct JobLogRow
{
    qint64 id = 0;
    QString runId;
    QString device;
    QDateTime time;
    QString step;
    QString status;      // ok / failed / skipped / info
    qint64 durationMs = 0;
    QString message;
    QString error;
    QString screenshot;  // path
};

/// job_runs + job_logs tables
class RunRepository
{
public:
    static QList<JobRunRow> loadRecent(int limit = 200);
    static bool saveRun(const JobRunRow &row);
    static bool appendLog(const JobLogRow &row);
    static bool appendLogs(const QList<JobLogRow> &rows);
    static QList<JobLogRow> loadLogs(const QString &runId, bool failuresOnly = false);
    static bool removeRun(const QString &runId);
    static int pruneOlderThan(int days);
};

/// activity table (persisted subset of the Activity Center)
class ActivityRepository
{
public:
    static bool append(int level, int category, const QString &device, const QString &message, const QDateTime &time);
    static int pruneOlderThan(int days);
};

/// kv table (small flags such as "legacy groups imported")
class KvRepository
{
public:
    static QString get(const QString &key, const QString &fallback = QString());
    static bool set(const QString &key, const QString &value);
};

} // namespace farm

#endif // FARM_STORAGE_REPOSITORIES_H
