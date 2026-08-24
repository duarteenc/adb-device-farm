#ifndef FARM_STORAGE_DATABASE_H
#define FARM_STORAGE_DATABASE_H

#include <QMutex>
#include <QObject>
#include <QSqlDatabase>
#include <QString>

namespace farm {

/**
 * SQLite storage for devices, groups, saved commands, templates, workflows,
 * schedules, job history and activity.
 *
 *  - One QSqlDatabase connection per thread (QSqlDatabase is not thread-safe).
 *  - Versioned migrations: `schema_version` holds the current version; every
 *    migration is applied in a transaction and never destroys existing data.
 *    A database newer than this build is left untouched and reported.
 *  - WAL journal so readers never block the GUI thread's writes.
 */
class Database : public QObject
{
    Q_OBJECT
public:
    static Database &instance();

    bool open(const QString &filePath);
    void close();
    bool isOpen() const { return m_open; }
    QString filePath() const { return m_path; }
    QString lastError() const { return m_lastError; }
    int schemaVersion() const { return m_version; }
    static int latestSchemaVersion();

    /// Connection bound to the calling thread (opened lazily).
    QSqlDatabase connection();

    /// Run `fn` inside a transaction on the calling thread's connection.
    bool transaction(const std::function<bool(QSqlDatabase &)> &fn);

    /// Copy the database file (WAL checkpointed) — used by Export Diagnostics.
    bool backupTo(const QString &destinationFile);

private:
    explicit Database(QObject *parent = nullptr);
    ~Database() override;
    bool migrate(QSqlDatabase &db);

    QMutex m_mutex;
    QString m_path;
    QString m_lastError;
    bool m_open = false;
    int m_version = 0;
};

} // namespace farm

#endif // FARM_STORAGE_DATABASE_H
