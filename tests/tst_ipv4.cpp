#include <QtTest>

#include "core/ipv4.h"

using namespace farm;

class TestIpv4 : public QObject
{
    Q_OBJECT
private slots:
    void parseAndFormat()
    {
        quint32 v = 0;
        QVERIFY(ipv4::parse(QStringLiteral("192.168.100.13"), v));
        QCOMPARE(v, 0xC0A8640Du);
        QCOMPARE(ipv4::toString(v), QStringLiteral("192.168.100.13"));
        QVERIFY(!ipv4::parse(QStringLiteral("192.168.100"), v));
        QVERIFY(!ipv4::parse(QStringLiteral("192.168.100.256"), v));
        QVERIFY(!ipv4::parse(QStringLiteral("a.b.c.d"), v));
        QVERIFY(!ipv4::parse(QStringLiteral("1.2.3.4.5"), v));
        QVERIFY(ipv4::parse(QStringLiteral(" 10.0.0.1 "), v));
        QVERIFY(ipv4::isValid(QStringLiteral("0.0.0.0")));
        QVERIFY(!ipv4::isValid(QStringLiteral("")));
    }

    void endpoints()
    {
        QCOMPARE(ipv4::hostOf(QStringLiteral("192.168.100.13:5555")), QStringLiteral("192.168.100.13"));
        QCOMPARE(ipv4::hostOf(QStringLiteral("ce0417123")), QStringLiteral("ce0417123"));
        QCOMPARE(ipv4::portOf(QStringLiteral("192.168.100.13:5555")), quint16(5555));
        QCOMPARE(ipv4::portOf(QStringLiteral("192.168.100.13"), 4444), quint16(4444));
        QCOMPARE(ipv4::portOf(QStringLiteral("192.168.100.13:abc"), 4444), quint16(4444));
        QVERIFY(ipv4::isTcpEndpoint(QStringLiteral("192.168.100.13:5555")));
        QVERIFY(!ipv4::isTcpEndpoint(QStringLiteral("192.168.100.13")));
        QVERIFY(!ipv4::isTcpEndpoint(QStringLiteral("R58M12345:5555")));
    }

    void cidrUsableRange()
    {
        ipv4::Range r;
        QVERIFY(ipv4::parseCidr(QStringLiteral("192.168.100.0/24"), r));
        QCOMPARE(ipv4::toString(r.first), QStringLiteral("192.168.100.1"));
        QCOMPARE(ipv4::toString(r.last), QStringLiteral("192.168.100.254"));
        QCOMPARE(r.count(), 254u);
        // Host bits set are ignored (192.168.100.180/24 is the same block).
        QVERIFY(ipv4::parseCidr(QStringLiteral("192.168.100.180/24"), r));
        QCOMPARE(ipv4::toString(r.first), QStringLiteral("192.168.100.1"));
        QVERIFY(ipv4::parseCidr(QStringLiteral("10.0.0.0/30"), r));
        QCOMPARE(r.count(), 2u);
        QVERIFY(ipv4::parseCidr(QStringLiteral("10.0.0.5/32"), r));
        QCOMPARE(r.count(), 1u);
        QCOMPARE(ipv4::toString(r.first), QStringLiteral("10.0.0.5"));
        QVERIFY(ipv4::parseCidr(QStringLiteral("10.0.0.0/31"), r));
        QCOMPARE(r.count(), 2u);
        QVERIFY(!ipv4::parseCidr(QStringLiteral("10.0.0.0/33"), r));
        QVERIFY(!ipv4::parseCidr(QStringLiteral("10.0.0.0"), r));
    }

    void ranges()
    {
        ipv4::Range r;
        QVERIFY(ipv4::parseRange(QStringLiteral("192.168.1.10-20"), r));
        QCOMPARE(r.count(), 11u);
        QCOMPARE(ipv4::toString(r.last), QStringLiteral("192.168.1.20"));
        QVERIFY(ipv4::parseRange(QStringLiteral("192.168.1.10-192.168.1.20"), r));
        QCOMPARE(r.count(), 11u);
        QVERIFY(ipv4::parseRange(QStringLiteral("192.168.1.10:5555-20"), r));
        QCOMPARE(r.count(), 11u);
        QVERIFY(ipv4::parseRange(QStringLiteral("192.168.1.10"), r));
        QCOMPARE(r.count(), 1u);
        QVERIFY(ipv4::parseRange(QStringLiteral("192.168.100.0/24"), r));
        QCOMPARE(r.count(), 254u);
        QVERIFY(!ipv4::parseRange(QStringLiteral("192.168.1.20-10"), r));
        QVERIFY(!ipv4::parseRange(QStringLiteral(""), r));
        QVERIFY(!ipv4::parseRange(QStringLiteral("192.168.1.10-300"), r));
    }

    void expandCap()
    {
        ipv4::Range r;
        QVERIFY(ipv4::parseCidr(QStringLiteral("192.168.100.0/24"), r));
        const QStringList all = ipv4::expand(r);
        QCOMPARE(all.size(), 254);
        QCOMPARE(all.first(), QStringLiteral("192.168.100.1"));
        QCOMPARE(all.last(), QStringLiteral("192.168.100.254"));
        QVERIFY(!all.contains(QStringLiteral("192.168.100.0")));
        QVERIFY(!all.contains(QStringLiteral("192.168.100.255")));
        QCOMPARE(ipv4::expand(r, 10).size(), 10);
    }

    void numericOrdering()
    {
        QStringList list{ QStringLiteral("192.168.100.10:5555"), QStringLiteral("192.168.100.9:5555"), QStringLiteral("R58M1234"),
                          QStringLiteral("192.168.100.100:5555"), QStringLiteral("10.0.0.1:5555"), QStringLiteral("A1B2") };
        std::sort(list.begin(), list.end(), ipv4::lessThan);
        QCOMPARE(list, (QStringList{ QStringLiteral("10.0.0.1:5555"), QStringLiteral("192.168.100.9:5555"), QStringLiteral("192.168.100.10:5555"),
                                     QStringLiteral("192.168.100.100:5555"), QStringLiteral("A1B2"), QStringLiteral("R58M1234") }));
        QCOMPARE(ipv4::lastOctet(QStringLiteral("192.168.100.13:5555")), 13);
        QCOMPARE(ipv4::lastOctet(QStringLiteral("R58M1234")), 0);
    }
};

QTEST_APPLESS_MAIN(TestIpv4)
#include "tst_ipv4.moc"
