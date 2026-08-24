#include <QSignalSpy>
#include <QTimer>
#include <QtTest>

#include "core/batchjob.h"

using namespace farm;

class TestBatchJob : public QObject
{
    Q_OBJECT
private slots:
    void boundedConcurrencyAndRetry()
    {
        QStringList ids;
        for (int i = 0; i < 12; ++i) {
            ids << QStringLiteral("d%1").arg(i);
        }
        int maxParallel = 0;
        int inFlight = 0;
        QSet<QString> failOnce{ QStringLiteral("d3"), QStringLiteral("d7") };
        QHash<QString, int> attempts;
        auto fn = [&](const QString &id, CancellationToken, BatchJob::DoneFn done) {
            ++inFlight;
            maxParallel = std::max(maxParallel, inFlight);
            attempts[id] += 1;
            const bool fail = failOnce.contains(id) && attempts[id] == 1;
            QTimer::singleShot(5, this, [&inFlight, done, fail]() {
                --inFlight;
                done(!fail, fail ? QStringLiteral("boom") : QStringLiteral("ok"));
            });
        };
        BatchJob job(QStringLiteral("test"), ids, fn, 3);
        QSignalSpy finished(&job, &BatchJob::finished);
        job.start();
        QVERIFY(finished.wait(5000));
        QCOMPARE(job.status(), BatchJob::Completed);    // partial failure still completes
        QCOMPARE(job.succeeded(), 10);
        QCOMPARE(job.failed(), 2);
        QCOMPARE(job.failedIds().size(), 2);
        QVERIFY(maxParallel <= 3);
        QVERIFY(maxParallel >= 2);
        // retry only the failed ones
        job.retryFailed();
        QVERIFY(finished.wait(5000));
        QCOMPARE(job.succeeded(), 12);
        QCOMPARE(job.failed(), 0);
        QCOMPARE(attempts.value(QStringLiteral("d3")), 2);
        QCOMPARE(attempts.value(QStringLiteral("d0")), 1);
        QVERIFY(job.summary().contains(QStringLiteral("12 / 12")));
    }

    void cancel()
    {
        QStringList ids;
        for (int i = 0; i < 20; ++i) {
            ids << QStringLiteral("d%1").arg(i);
        }
        auto fn = [this](const QString &, CancellationToken token, BatchJob::DoneFn done) {
            QTimer::singleShot(30, this, [done, token]() { done(!token.isCancelled(), token.isCancelled() ? QStringLiteral("cancelled") : QStringLiteral("ok")); });
        };
        BatchJob job(QStringLiteral("cancel"), ids, fn, 2);
        QSignalSpy finished(&job, &BatchJob::finished);
        job.start();
        QTest::qWait(40);
        job.cancel();
        QVERIFY(finished.wait(3000));
        QCOMPARE(job.status(), BatchJob::Cancelled);
        QVERIFY(job.succeeded() < 20);
        QVERIFY(job.cancelled() > 0);
        QCOMPARE(job.queued(), 0);
        QCOMPARE(job.running(), 0);
    }

    void allFail()
    {
        auto fn = [this](const QString &, CancellationToken, BatchJob::DoneFn done) {
            QTimer::singleShot(1, this, [done]() { done(false, QStringLiteral("no")); });
        };
        BatchJob job(QStringLiteral("fail"), { QStringLiteral("a"), QStringLiteral("b") }, fn, 4);
        QSignalSpy finished(&job, &BatchJob::finished);
        job.start();
        QVERIFY(finished.wait(3000));
        QCOMPARE(job.status(), BatchJob::Failed);
        QCOMPARE(job.item(QStringLiteral("a")).message, QStringLiteral("no"));
    }

    void manager()
    {
        JobManager &m = JobManager::instance();
        auto fn = [this](const QString &, CancellationToken, BatchJob::DoneFn done) { QTimer::singleShot(1, this, [done]() { done(true, QString()); }); };
        BatchJob *job = new BatchJob(QStringLiteral("m"), { QStringLiteral("a") }, fn, 1);
        QSignalSpy finished(job, &BatchJob::finished);
        m.startJob(job);
        QCOMPARE(m.jobs().size(), 1);
        QVERIFY(finished.wait(3000));
        QCOMPARE(m.activeCount(), 0);
        m.clearFinished();
        QCOMPARE(m.jobs().size(), 0);
    }
};

QTEST_GUILESS_MAIN(TestBatchJob)
#include "tst_batchjob.moc"
