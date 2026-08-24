#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

#include "adb/adbparsers.h"
#include "devices/deviceregistry.h"
#include "storage/database.h"

using namespace farm;

class TestRegistry : public QObject
{
    Q_OBJECT
    QTemporaryDir m_dir;

private slots:
    void initTestCase()
    {
        QVERIFY(m_dir.isValid());
        QVERIFY(Database::instance().open(m_dir.filePath(QStringLiteral("farm.db"))));
        DeviceRegistry::instance().load();
        QCOMPARE(DeviceRegistry::instance().count(), 0);
    }

    void upsertAndStates()
    {
        DeviceRegistry &reg = DeviceRegistry::instance();
        QSignalSpy added(&reg, &DeviceRegistry::deviceAdded);
        QSignalSpy changed(&reg, &DeviceRegistry::stateChanged);
        const QList<adb::AdbDeviceInfo> list = adb::parseDevicesList(QStringLiteral(
            "List of devices attached\n"
            "192.168.100.10:5555 device product:p model:SM_G9500 device:d transport_id:1\n"
            "192.168.100.9:5555  unauthorized transport_id:2\n"
            "R58M1               device model:SM_G973F transport_id:3\n"));
        for (const adb::AdbDeviceInfo &i : list) {
            reg.upsertFromAdb(i);
        }
        QCOMPARE(added.count(), 3);
        QCOMPARE(reg.count(), 3);
        QCOMPARE(reg.get(QStringLiteral("192.168.100.10:5555")).state, DeviceState::AdbOnline);
        QCOMPARE(reg.get(QStringLiteral("192.168.100.10:5555")).model, QStringLiteral("SM-G9500"));
        QCOMPARE(reg.get(QStringLiteral("192.168.100.10:5555")).connectionType, ConnectionType::WifiAdb);
        QCOMPARE(reg.get(QStringLiteral("192.168.100.9:5555")).state, DeviceState::Unauthorized);
        QCOMPARE(reg.get(QStringLiteral("R58M1")).connectionType, ConnectionType::Usb);
        QVERIFY(changed.count() >= 3);
        // Mirroring state is owned by DeviceService and must not be clobbered by adb refreshes.
        reg.setState(QStringLiteral("192.168.100.10:5555"), DeviceState::Mirroring);
        reg.upsertFromAdb(list[0]);
        QCOMPARE(reg.get(QStringLiteral("192.168.100.10:5555")).state, DeviceState::Mirroring);
        QCOMPARE(reg.onlineIds().size(), 2);
        // Discovered host
        const QString id = reg.markDiscovered(QStringLiteral("192.168.100.200"), 5555);
        QCOMPARE(id, QStringLiteral("192.168.100.200:5555"));
        QCOMPARE(reg.get(id).state, DeviceState::Discovered);
    }

    void numberingAndSorting()
    {
        DeviceRegistry &reg = DeviceRegistry::instance();
        reg.autoNumber();
        // numeric IP order: .9 (1), .10 (2), .200 (3), then USB serial (4)
        QCOMPARE(reg.get(QStringLiteral("192.168.100.9:5555")).number, 1);
        QCOMPARE(reg.get(QStringLiteral("192.168.100.10:5555")).number, 2);
        QCOMPARE(reg.get(QStringLiteral("192.168.100.200:5555")).number, 3);
        QCOMPARE(reg.get(QStringLiteral("R58M1")).number, 4);
        QCOMPARE(reg.nextFreeNumber(), 5);
        const QStringList byIp = reg.sorted(DeviceRegistry::SortKey::Ip);
        QCOMPARE(byIp.first(), QStringLiteral("192.168.100.9:5555"));
        QCOMPARE(byIp.at(1), QStringLiteral("192.168.100.10:5555"));
        reg.setNumber(QStringLiteral("R58M1"), 1);
        reg.setNumber(QStringLiteral("192.168.100.9:5555"), 10);
        const QStringList byNumber = reg.sorted(DeviceRegistry::SortKey::Number);
        QCOMPARE(byNumber.first(), QStringLiteral("R58M1"));
        QCOMPARE(byNumber.last(), QStringLiteral("192.168.100.9:5555"));
        reg.renumber({ QStringLiteral("192.168.100.200:5555"), QStringLiteral("192.168.100.10:5555") }, 100);
        QCOMPARE(reg.get(QStringLiteral("192.168.100.200:5555")).number, 100);
        QCOMPARE(reg.get(QStringLiteral("192.168.100.10:5555")).number, 101);
        const QStringList byOnline = reg.sorted(DeviceRegistry::SortKey::Online);
        QVERIFY(reg.get(byOnline.first()).isOnline());
        QVERIFY(!reg.get(byOnline.last()).isOnline());
    }

    void groups()
    {
        DeviceRegistry &reg = DeviceRegistry::instance();
        QSignalSpy spy(&reg, &DeviceRegistry::groupsChanged);
        QVERIFY(reg.createGroup(QStringLiteral("Box 1")));
        QVERIFY(!reg.createGroup(QStringLiteral("Box 1")));
        QVERIFY(!reg.createGroup(QStringLiteral("  ")));
        reg.assignGroup({ QStringLiteral("192.168.100.9:5555"), QStringLiteral("192.168.100.10:5555") }, QStringLiteral("Box 1"));
        QCOMPARE(reg.membersOf(QStringLiteral("Box 1")).size(), 2);
        QVERIFY(reg.renameGroup(QStringLiteral("Box 1"), QStringLiteral("Box A")));
        QCOMPARE(reg.membersOf(QStringLiteral("Box A")).size(), 2);
        QCOMPARE(reg.get(QStringLiteral("192.168.100.9:5555")).group, QStringLiteral("Box A"));
        reg.assignGroup({ QStringLiteral("192.168.100.9:5555") }, QString());
        QCOMPARE(reg.membersOf(QStringLiteral("Box A")).size(), 1);
        QVERIFY(reg.setGroupColor(QStringLiteral("Box A"), QStringLiteral("#ff0000")));
        QCOMPARE(reg.group(QStringLiteral("Box A")).color, QStringLiteral("#ff0000"));
        reg.assignGroup({ QStringLiteral("R58M1") }, QStringLiteral("Auto Created"));    // implicit create
        QVERIFY(reg.hasGroup(QStringLiteral("Auto Created")));
        QVERIFY(reg.deleteGroup(QStringLiteral("Box A")));
        QVERIFY(reg.get(QStringLiteral("192.168.100.10:5555")).group.isEmpty());
        QVERIFY(spy.count() >= 6);
    }

    void searchAndFilters()
    {
        DeviceRegistry &reg = DeviceRegistry::instance();
        reg.update(QStringLiteral("R58M1"), [](DeviceRecord &r) {
            r.friendlyName = QStringLiteral("Front desk");
            r.favorite = true;
        });
        QCOMPARE(reg.search(QStringLiteral("front")), QStringList{ QStringLiteral("R58M1") });
        QCOMPARE(reg.search(QStringLiteral("100.10")), QStringList{ QStringLiteral("192.168.100.10:5555") });
        QVERIFY(reg.search(QStringLiteral("G9500")).contains(QStringLiteral("192.168.100.10:5555")));
        QCOMPARE(reg.favorites(), QStringList{ QStringLiteral("R58M1") });
        QVERIFY(reg.byModel(QStringLiteral("sm-g9500")).contains(QStringLiteral("192.168.100.10:5555")));
    }

    void persistence()
    {
        DeviceRegistry &reg = DeviceRegistry::instance();
        reg.flush();
        const QList<DeviceRecord> rows = DeviceRepository::loadAll();
        QCOMPARE(rows.size(), 4);
        bool found = false;
        for (const DeviceRecord &r : rows) {
            if (r.id == QLatin1String("R58M1")) {
                found = true;
                QCOMPARE(r.friendlyName, QStringLiteral("Front desk"));
                QCOMPARE(r.group, QStringLiteral("Auto Created"));
                QVERIFY(r.favorite);
            }
        }
        QVERIFY(found);
        reg.remove(QStringLiteral("R58M1"));
        QCOMPARE(DeviceRepository::loadAll().size(), 3);
    }

    void cleanupTestCase()
    {
        Database::instance().close();
    }
};

QTEST_GUILESS_MAIN(TestRegistry)
#include "tst_registry.moc"
