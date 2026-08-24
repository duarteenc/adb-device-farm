#include <QtTest>

#include "scheduler/scheduler.h"

using namespace farm;

class TestScheduler : public QObject
{
    Q_OBJECT
private slots:
    void nextRunComputation()
    {
        const QDateTime from(QDate(2026, 8, 24), QTime(10, 30));    // Monday
        Schedule s;
        s.kind = QStringLiteral("daily");
        s.timeOfDay = QTime(9, 0);
        QCOMPARE(Scheduler::computeNextRun(s, from), QDateTime(QDate(2026, 8, 25), QTime(9, 0)));
        s.timeOfDay = QTime(11, 0);
        QCOMPARE(Scheduler::computeNextRun(s, from), QDateTime(QDate(2026, 8, 24), QTime(11, 0)));

        s.kind = QStringLiteral("hourly");
        s.timeOfDay = QTime(0, 15);
        QCOMPARE(Scheduler::computeNextRun(s, from), QDateTime(QDate(2026, 8, 24), QTime(11, 15)));
        s.timeOfDay = QTime(0, 45);
        QCOMPARE(Scheduler::computeNextRun(s, from), QDateTime(QDate(2026, 8, 24), QTime(10, 45)));

        s.kind = QStringLiteral("interval");
        s.intervalMinutes = 30;
        s.lastRun = QDateTime();
        QCOMPARE(Scheduler::computeNextRun(s, from), from.addSecs(1800));
        s.lastRun = QDateTime(QDate(2026, 8, 24), QTime(8, 0));    // long ago: skips ahead past now
        QCOMPARE(Scheduler::computeNextRun(s, from), QDateTime(QDate(2026, 8, 24), QTime(11, 0)));

        s.kind = QStringLiteral("weekly");
        s.timeOfDay = QTime(9, 0);
        s.weekdays = { 3, 5 };    // Wed, Fri
        QCOMPARE(Scheduler::computeNextRun(s, from), QDateTime(QDate(2026, 8, 26), QTime(9, 0)));
        s.weekdays = { 1 };    // Monday but 09:00 already passed -> next Monday
        QCOMPARE(Scheduler::computeNextRun(s, from), QDateTime(QDate(2026, 8, 31), QTime(9, 0)));
        s.weekdays.clear();
        QVERIFY(!Scheduler::computeNextRun(s, from).isValid());

        s.kind = QStringLiteral("once");
        s.onceAt = QDateTime(QDate(2026, 8, 24), QTime(12, 0));
        QCOMPARE(Scheduler::computeNextRun(s, from), s.onceAt);
        s.onceAt = QDateTime(QDate(2026, 8, 24), QTime(9, 0));
        QVERIFY(!Scheduler::computeNextRun(s, from).isValid());    // in the past: never

        s.kind = QStringLiteral("event");
        QVERIFY(!Scheduler::computeNextRun(s, from).isValid());
    }

    void jsonRoundTrip()
    {
        Schedule s;
        s.id = QStringLiteral("s1");
        s.name = QStringLiteral("Nightly");
        s.workflowId = QStringLiteral("wf");
        s.targetsMode = QStringLiteral("group");
        s.group = QStringLiteral("Box 1");
        s.kind = QStringLiteral("weekly");
        s.weekdays = { 1, 2, 3 };
        s.timeOfDay = QTime(22, 30);
        s.missedPolicy = QStringLiteral("nextStart");
        s.eventThreshold = 15;
        const Schedule back = Schedule::fromJson(s.toJson());
        QCOMPARE(back.id, s.id);
        QCOMPARE(back.group, s.group);
        QCOMPARE(back.weekdays, s.weekdays);
        QCOMPARE(back.timeOfDay, s.timeOfDay);
        QCOMPARE(back.missedPolicy, s.missedPolicy);
        QCOMPARE(back.kind, s.kind);
        QVERIFY(back.describe().contains(QStringLiteral("Mon,Tue,Wed")));
    }
};

QTEST_APPLESS_MAIN(TestScheduler)
#include "tst_scheduler.moc"
