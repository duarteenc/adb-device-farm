#include <QSqlQuery>
#include <QTemporaryDir>
#include <QtTest>

#include "storage/database.h"
#include "storage/repositories.h"

using namespace farm;

class TestDatabase : public QObject
{
    Q_OBJECT
private slots:
    void migrationsAndReopen()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.filePath(QStringLiteral("farm.db"));
        Database &db = Database::instance();
        QVERIFY(db.open(path));
        QCOMPARE(db.schemaVersion(), Database::latestSchemaVersion());

        // Data written before a reopen survives (migrations are idempotent).
        DeviceRecord r;
        r.id = QStringLiteral("192.168.100.13:5555");
        r.model = QStringLiteral("SM-G9500");
        r.number = 7;
        r.group = QStringLiteral("Box 1");
        r.favorite = true;
        r.props.insert(QStringLiteral("ro.build.version.sdk"), 28);
        QVERIFY(DeviceRepository::save(r));
        db.close();
        QVERIFY(db.open(path));
        const QList<DeviceRecord> all = DeviceRepository::loadAll();
        QCOMPARE(all.size(), 1);
        QCOMPARE(all[0].model, QStringLiteral("SM-G9500"));
        QCOMPARE(all[0].number, 7);
        QCOMPARE(all[0].group, QStringLiteral("Box 1"));
        QVERIFY(all[0].favorite);
        QCOMPARE(all[0].props.value(QStringLiteral("ro.build.version.sdk")).toInt(), 28);
        QCOMPARE(all[0].state, DeviceState::Offline);

        // Saved commands are seeded once and only once.
        CommandRepository::seedDefaultsIfEmpty();
        const int seeded = static_cast<int>(CommandRepository::loadAll().size());
        QVERIFY(seeded > 10);
        CommandRepository::seedDefaultsIfEmpty();
        QCOMPARE(static_cast<int>(CommandRepository::loadAll().size()), seeded);

        // kv, templates, workflows, schedules, runs round-trip.
        QVERIFY(KvRepository::set(QStringLiteral("k"), QStringLiteral("v")));
        QCOMPARE(KvRepository::get(QStringLiteral("k")), QStringLiteral("v"));
        TextTemplate t;
        t.name = QStringLiteral("Greeting");
        t.content = QStringLiteral("hola");
        const qint64 tid = TemplateRepository::save(t);
        QVERIFY(tid > 0);
        QCOMPARE(TemplateRepository::loadAll().size(), 1);
        WorkflowRow w;
        w.id = QStringLiteral("wf1");
        w.name = QStringLiteral("Test");
        w.json = QStringLiteral("{}");
        QVERIFY(WorkflowRepository::save(w));
        QCOMPARE(WorkflowRepository::load(QStringLiteral("wf1")).name, QStringLiteral("Test"));
        JobRunRow run;
        run.id = QStringLiteral("run1");
        run.kind = QStringLiteral("workflow");
        run.started = QDateTime::currentDateTime();
        run.status = QStringLiteral("Running");
        QVERIFY(RunRepository::saveRun(run));
        JobLogRow log;
        log.runId = QStringLiteral("run1");
        log.device = r.id;
        log.step = QStringLiteral("Tap");
        log.status = QStringLiteral("failed");
        log.error = QStringLiteral("boom");
        QVERIFY(RunRepository::appendLog(log));
        QCOMPARE(RunRepository::loadLogs(QStringLiteral("run1"), true).size(), 1);
        QCOMPARE(RunRepository::loadRecent().size(), 1);
        db.close();
    }

    void refusesNewerSchema()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("future.db"));
        Database &db = Database::instance();
        QVERIFY(db.open(path));
        {
            QSqlQuery q(db.connection());
            QVERIFY(q.exec(QStringLiteral("UPDATE schema_version SET version=999")));
        }
        db.close();
        QVERIFY(!db.open(path));    // never touch data written by a newer build
        QVERIFY(db.lastError().contains(QStringLiteral("newer")));
        db.close();
    }
};

QTEST_GUILESS_MAIN(TestDatabase)
#include "tst_database.moc"
