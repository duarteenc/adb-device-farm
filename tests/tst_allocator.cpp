#include <QtTest>

#include "devices/connectionidallocator.h"

using namespace farm;

class TestAllocator : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase()
    {
        ConnectionIdAllocator::instance().setProbeBinding(false);
        ConnectionIdAllocator::instance().setPortRange(30000, 300);
    }

    void uniqueLeases()
    {
        ConnectionIdAllocator &a = ConnectionIdAllocator::instance();
        QSet<quint32> scids;
        QSet<quint16> ports;
        for (int i = 0; i < 200; ++i) {
            const ConnectionIdAllocator::Lease lease = a.acquire(QStringLiteral("dev-%1").arg(i));
            QVERIFY(lease.valid());
            QVERIFY(!scids.contains(lease.scid));
            QVERIFY(!ports.contains(lease.localPort));
            QVERIFY(lease.scid > 0 && lease.scid < 0x80000000u);
            QVERIFY(lease.localPort >= 30000 && lease.localPort < 30300);
            scids.insert(lease.scid);
            ports.insert(lease.localPort);
        }
        QCOMPARE(a.inUseCount(), 200);
        // Same owner returns the same lease (idempotent).
        const ConnectionIdAllocator::Lease again = a.acquire(QStringLiteral("dev-0"));
        QCOMPARE(again.localPort, a.owners().value(QStringLiteral("dev-0")).localPort);
        QCOMPARE(a.inUseCount(), 200);
        for (int i = 0; i < 200; ++i) {
            a.release(QStringLiteral("dev-%1").arg(i));
        }
        QCOMPARE(a.inUseCount(), 0);
    }

    void releaseAndNoImmediateReuse()
    {
        ConnectionIdAllocator &a = ConnectionIdAllocator::instance();
        const ConnectionIdAllocator::Lease first = a.acquire(QStringLiteral("x"));
        a.release(QStringLiteral("x"));
        QVERIFY(!a.isPortInUse(first.localPort));
        QVERIFY(!a.isScidInUse(first.scid));
        const ConnectionIdAllocator::Lease second = a.acquire(QStringLiteral("y"));
        QVERIFY(second.localPort != first.localPort);    // round-robin skips the just-released port
        a.release(QStringLiteral("y"));
    }

    void exhaustion()
    {
        ConnectionIdAllocator &a = ConnectionIdAllocator::instance();
        a.setPortRange(31000, 3);
        QVERIFY(a.acquire(QStringLiteral("a")).valid());
        QVERIFY(a.acquire(QStringLiteral("b")).valid());
        QVERIFY(a.acquire(QStringLiteral("c")).valid());
        QVERIFY(!a.acquire(QStringLiteral("d")).valid());
        a.release(QStringLiteral("b"));
        QVERIFY(a.acquire(QStringLiteral("d")).valid());
        for (const QString &o : { QStringLiteral("a"), QStringLiteral("c"), QStringLiteral("d") }) {
            a.release(o);
        }
        QCOMPARE(a.inUseCount(), 0);
    }
};

QTEST_GUILESS_MAIN(TestAllocator)
#include "tst_allocator.moc"
