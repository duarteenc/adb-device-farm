#include "database.h"

#include <functional>

#include <QFile>
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QSqlError>
#include <QSqlQuery>
#include <QThread>

#include "../core/farmlog.h"

namespace farm {

namespace {
struct Migration
{
    int version;
    const char *const *statements;
};

// ---- migration 001: initial schema ----
const char *const kMigration001[] = {
    "CREATE TABLE IF NOT EXISTS schema_version (version INTEGER NOT NULL)",
    "CREATE TABLE IF NOT EXISTS devices ("
    "  id TEXT PRIMARY KEY,"
    "  hw_serial TEXT,"
    "  last_ip TEXT,"
    "  port INTEGER DEFAULT 5555,"
    "  model TEXT,"
    "  manufacturer TEXT,"
    "  android_version TEXT,"
    "  sdk INTEGER DEFAULT 0,"
    "  friendly_name TEXT,"
    "  number INTEGER DEFAULT 0,"
    "  group_name TEXT,"
    "  connection_type INTEGER DEFAULT 0,"
    "  first_seen INTEGER,"
    "  last_seen INTEGER,"
    "  favorite INTEGER DEFAULT 0,"
    "  notes TEXT,"
    "  bitrate INTEGER DEFAULT 0,"
    "  fps INTEGER DEFAULT 0,"
    "  max_size INTEGER DEFAULT 0,"
    "  keep_awake INTEGER DEFAULT -1,"
    "  auto_connect INTEGER DEFAULT 1,"
    "  auto_mirror INTEGER DEFAULT 1,"
    "  props TEXT"
    ")",
    "CREATE INDEX IF NOT EXISTS idx_devices_group ON devices(group_name)",
    "CREATE INDEX IF NOT EXISTS idx_devices_hw ON devices(hw_serial)",
    "CREATE TABLE IF NOT EXISTS groups ("
    "  name TEXT PRIMARY KEY,"
    "  color TEXT,"
    "  sort_order INTEGER DEFAULT 0,"
    "  settings TEXT"
    ")",
    "CREATE TABLE IF NOT EXISTS saved_commands ("
    "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
    "  name TEXT NOT NULL,"
    "  command TEXT NOT NULL,"
    "  category TEXT,"
    "  description TEXT,"
    "  sort_order INTEGER DEFAULT 0"
    ")",
    "CREATE TABLE IF NOT EXISTS text_templates ("
    "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
    "  name TEXT NOT NULL,"
    "  category TEXT,"
    "  content TEXT NOT NULL,"
    "  shortcut TEXT"
    ")",
    "CREATE TABLE IF NOT EXISTS workflows ("
    "  id TEXT PRIMARY KEY,"
    "  name TEXT NOT NULL,"
    "  json TEXT NOT NULL,"
    "  created INTEGER,"
    "  updated INTEGER"
    ")",
    "CREATE TABLE IF NOT EXISTS schedules ("
    "  id TEXT PRIMARY KEY,"
    "  name TEXT,"
    "  json TEXT NOT NULL,"
    "  enabled INTEGER DEFAULT 1,"
    "  next_run INTEGER,"
    "  last_run INTEGER"
    ")",
    "CREATE TABLE IF NOT EXISTS job_runs ("
    "  id TEXT PRIMARY KEY,"
    "  kind TEXT,"
    "  name TEXT,"
    "  workflow_id TEXT,"
    "  started INTEGER,"
    "  finished INTEGER,"
    "  status TEXT,"
    "  total INTEGER DEFAULT 0,"
    "  succeeded INTEGER DEFAULT 0,"
    "  failed INTEGER DEFAULT 0,"
    "  json TEXT"
    ")",
    "CREATE INDEX IF NOT EXISTS idx_job_runs_started ON job_runs(started)",
    "CREATE TABLE IF NOT EXISTS job_logs ("
    "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
    "  run_id TEXT NOT NULL,"
    "  device TEXT,"
    "  ts INTEGER,"
    "  step TEXT,"
    "  status TEXT,"
    "  duration INTEGER DEFAULT 0,"
    "  message TEXT,"
    "  error TEXT,"
    "  screenshot TEXT"
    ")",
    "CREATE INDEX IF NOT EXISTS idx_job_logs_run ON job_logs(run_id)",
    "CREATE TABLE IF NOT EXISTS activity ("
    "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
    "  ts INTEGER,"
    "  level INTEGER,"
    "  category INTEGER,"
    "  device TEXT,"
    "  message TEXT"
    ")",
    "CREATE INDEX IF NOT EXISTS idx_activity_ts ON activity(ts)",
    "CREATE TABLE IF NOT EXISTS kv (key TEXT PRIMARY KEY, value TEXT)",
    nullptr,
};

// ---- migration 002: per-device profile columns + group settings ----
const char *const kMigration002[] = {
    "ALTER TABLE devices ADD COLUMN preset TEXT",
    "ALTER TABLE devices ADD COLUMN pinned_order INTEGER DEFAULT 0",
    nullptr,
};

const Migration kMigrations[] = {
    { 1, kMigration001 },
    { 2, kMigration002 },
};
} // namespace

Database &Database::instance()
{
    static Database db;
    return db;
}

Database::Database(QObject *parent)
    : QObject(parent)
{
}

Database::~Database()
{
    if (QCoreApplication::instance()) {
        close();
    }
}

int Database::latestSchemaVersion()
{
    return kMigrations[sizeof(kMigrations) / sizeof(kMigrations[0]) - 1].version;
}

bool Database::open(const QString &filePath)
{
    QMutexLocker lock(&m_mutex);
    m_path = filePath;
    QDir().mkpath(QFileInfo(filePath).absolutePath());
    m_open = true;    // so connection() works below
    lock.unlock();

    QSqlDatabase db = connection();
    if (!db.isOpen()) {
        QMutexLocker relock(&m_mutex);
        m_open = false;
        return false;
    }
    if (!migrate(db)) {
        QMutexLocker relock(&m_mutex);
        m_open = false;
        return false;
    }
    FarmLog::instance().info(QStringLiteral("storage"), QStringLiteral("database open: %1 (schema v%2)").arg(m_path).arg(m_version));
    return true;
}

void Database::close()
{
    QMutexLocker lock(&m_mutex);
    m_open = false;
    const QStringList names = QSqlDatabase::connectionNames();
    for (const QString &name : names) {
        if (name.startsWith(QLatin1String("farm-"))) {
            {
                QSqlDatabase db = QSqlDatabase::database(name, false);
                if (db.isOpen()) {
                    db.close();
                }
            }
            QSqlDatabase::removeDatabase(name);
        }
    }
}

QSqlDatabase Database::connection()
{
    if (!m_open) {
        return QSqlDatabase();
    }
    const QString name = QStringLiteral("farm-%1").arg(reinterpret_cast<quintptr>(QThread::currentThreadId()));
    if (QSqlDatabase::contains(name)) {
        QSqlDatabase db = QSqlDatabase::database(name);
        if (db.isOpen()) {
            return db;
        }
    }
    QSqlDatabase db = QSqlDatabase::contains(name) ? QSqlDatabase::database(name) : QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), name);
    db.setDatabaseName(m_path);
    db.setConnectOptions(QStringLiteral("QSQLITE_BUSY_TIMEOUT=5000"));
    if (!db.open()) {
        QMutexLocker lock(&m_mutex);
        m_lastError = db.lastError().text();
        FarmLog::instance().error(QStringLiteral("storage"), QStringLiteral("cannot open %1: %2").arg(m_path, m_lastError));
        return db;
    }
    QSqlQuery pragma(db);
    pragma.exec(QStringLiteral("PRAGMA journal_mode=WAL"));
    pragma.exec(QStringLiteral("PRAGMA synchronous=NORMAL"));
    pragma.exec(QStringLiteral("PRAGMA foreign_keys=ON"));
    return db;
}

bool Database::migrate(QSqlDatabase &db)
{
    QSqlQuery q(db);
    int current = 0;
    if (q.exec(QStringLiteral("SELECT name FROM sqlite_master WHERE type='table' AND name='schema_version'")) && q.next()) {
        QSqlQuery v(db);
        if (v.exec(QStringLiteral("SELECT MAX(version) FROM schema_version")) && v.next()) {
            current = v.value(0).toInt();
        }
    }
    const int latest = latestSchemaVersion();
    if (current > latest) {
        m_lastError = QStringLiteral("database schema v%1 is newer than this build supports (v%2)").arg(current).arg(latest);
        FarmLog::instance().error(QStringLiteral("storage"), m_lastError);
        return false;
    }
    for (const Migration &m : kMigrations) {
        if (m.version <= current) {
            continue;
        }
        if (!db.transaction()) {
            m_lastError = db.lastError().text();
            return false;
        }
        bool ok = true;
        for (const char *const *stmt = m.statements; *stmt; ++stmt) {
            QSqlQuery s(db);
            if (!s.exec(QString::fromUtf8(*stmt))) {
                // ALTER TABLE ADD COLUMN on an existing column is the only benign failure.
                const QString err = s.lastError().text();
                if (err.contains(QLatin1String("duplicate column"), Qt::CaseInsensitive)) {
                    continue;
                }
                m_lastError = QStringLiteral("migration %1 failed: %2 [%3]").arg(m.version).arg(err, QString::fromUtf8(*stmt));
                ok = false;
                break;
            }
        }
        if (ok) {
            QSqlQuery s(db);
            s.exec(QStringLiteral("DELETE FROM schema_version"));
            s.prepare(QStringLiteral("INSERT INTO schema_version(version) VALUES(?)"));
            s.addBindValue(m.version);
            ok = s.exec();
        }
        if (!ok) {
            db.rollback();
            FarmLog::instance().error(QStringLiteral("storage"), m_lastError);
            return false;
        }
        db.commit();
        FarmLog::instance().info(QStringLiteral("storage"), QStringLiteral("applied migration %1").arg(m.version));
        current = m.version;
    }
    m_version = current;
    return true;
}

bool Database::transaction(const std::function<bool(QSqlDatabase &)> &fn)
{
    QSqlDatabase db = connection();
    if (!db.isOpen()) {
        return false;
    }
    if (!db.transaction()) {
        return false;
    }
    if (!fn(db)) {
        db.rollback();
        return false;
    }
    return db.commit();
}

bool Database::backupTo(const QString &destinationFile)
{
    QSqlDatabase db = connection();
    if (!db.isOpen()) {
        return false;
    }
    QSqlQuery q(db);
    q.exec(QStringLiteral("PRAGMA wal_checkpoint(TRUNCATE)"));
    QFile::remove(destinationFile);
    return QFile::copy(m_path, destinationFile);
}

} // namespace farm
