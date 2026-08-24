#include <QtTest>

#include "adb/adbparsers.h"
#include "adb/adbquote.h"

using namespace farm;
using namespace farm::adb;

class TestAdbParsers : public QObject
{
    Q_OBJECT
private slots:
    void devicesList()
    {
        const QString out = QStringLiteral(
            "List of devices attached\n"
            "192.168.100.104:5555   device product:dreamlteks model:SM_G950N device:dreamlteks transport_id:3\n"
            "192.168.100.13:5555    unauthorized transport_id:1\n"
            "R58M12345              device usb:1-2 product:beyond1 model:SM_G973F device:beyond1 transport_id:7\n"
            "0123456789ABCDEF       no permissions (user in plugdev group); see [http://developer.android.com/tools/device.html]\n"
            "192.168.100.99:5555    offline transport_id:9\n");
        const QList<AdbDeviceInfo> list = parseDevicesList(out);
        QCOMPARE(list.size(), 5);
        QCOMPARE(list[0].serial, QStringLiteral("192.168.100.104:5555"));
        QVERIFY(list[0].isOnline());
        QVERIFY(list[0].isTcp);
        QCOMPARE(list[0].model, QStringLiteral("SM_G950N"));
        QCOMPARE(list[0].product, QStringLiteral("dreamlteks"));
        QCOMPARE(list[0].transportId, QStringLiteral("3"));
        QCOMPARE(list[1].state, QStringLiteral("unauthorized"));
        QVERIFY(!list[2].isTcp);
        QCOMPARE(list[2].model, QStringLiteral("SM_G973F"));
        QCOMPARE(list[3].state, QStringLiteral("no permissions"));
        QCOMPARE(list[4].state, QStringLiteral("offline"));
        QVERIFY(parseDevicesList(QStringLiteral("List of devices attached\n\n")).isEmpty());
    }

    void mdns()
    {
        const QString out = QStringLiteral("List of discovered mdns services\nadb-R58M12345-AbCdEf\t_adb-tls-connect._tcp\t192.168.100.50:37211\nadb-legacy\t_adb._tcp\t192.168.100.51:5555\n");
        const QList<MdnsService> s = parseMdnsServices(out);
        QCOMPARE(s.size(), 2);
        QCOMPARE(s[1].type, QStringLiteral("_adb._tcp"));
        QCOMPARE(s[1].address, QStringLiteral("192.168.100.51:5555"));
    }

    void getprop()
    {
        const QHash<QString, QString> p = parseGetProp(QStringLiteral("[ro.product.model]: [SM-G9500]\n[ro.build.version.sdk]: [28]\n[weird]: [a]: [b]\n"));
        QCOMPARE(p.value(QStringLiteral("ro.product.model")), QStringLiteral("SM-G9500"));
        QCOMPARE(p.value(QStringLiteral("ro.build.version.sdk")), QStringLiteral("28"));
    }

    void battery()
    {
        const BatteryInfo b = parseBattery(QStringLiteral("Current Battery Service state:\n  AC powered: false\n  USB powered: true\n  status: 2\n  level: 84\n  temperature: 312\n  voltage: 4123\n"));
        QCOMPARE(b.level, 84);
        QCOMPARE(b.status, 2);
        QVERIFY(b.usbPowered);
        QVERIFY(b.charging());
        QCOMPARE(b.temperatureC, 31.2);
        QCOMPARE(b.voltageMv, 4123);
    }

    void packages()
    {
        const QList<PackageInfo> p = parsePackages(QStringLiteral("package:/data/app/com.example-1/base.apk=com.example\npackage:com.android.settings\n"));
        QCOMPARE(p.size(), 2);
        QCOMPARE(p[0].name, QStringLiteral("com.example"));
        QCOMPARE(p[0].apkPath, QStringLiteral("/data/app/com.example-1/base.apk"));
        QCOMPARE(p[1].name, QStringLiteral("com.android.settings"));
        QVERIFY(p[1].apkPath.isEmpty());
    }

    void lsLa()
    {
        const QString toybox = QStringLiteral(
            "total 24\n"
            "drwxrwx--x  4 root sdcard_rw 4096 2026-08-24 10:11 .\n"
            "drwxrwx--x 20 root sdcard_rw 4096 2026-08-20 09:00 ..\n"
            "drwxrwx--x  2 root sdcard_rw 4096 2026-08-24 10:11 Download\n"
            "-rw-rw----  1 root sdcard_rw 12345 2026-08-23 18:30 my photo.jpg\n"
            "lrwxrwxrwx  1 root root 21 2026-01-01 00:00 link -> /sdcard/Download\n");
        const QList<RemoteEntry> e = parseLsLa(toybox);
        QCOMPARE(e.size(), 3);
        QVERIFY(e[0].isDir);
        QCOMPARE(e[0].name, QStringLiteral("Download"));
        QCOMPARE(e[1].name, QStringLiteral("my photo.jpg"));
        QCOMPARE(e[1].size, qint64(12345));
        QVERIFY(e[2].isLink);
        QCOMPARE(e[2].name, QStringLiteral("link"));
        const QString toolbox = QStringLiteral("drwxrwx--x root sdcard_rw 2026-08-24 10:11 DCIM\n-rw-rw---- root sdcard_rw 100 2026-08-24 10:11 a.txt\n");
        const QList<RemoteEntry> t = parseLsLa(toolbox);
        QCOMPARE(t.size(), 2);
        QCOMPARE(t[1].size, qint64(100));
    }

    void screenState()
    {
        ScreenState s = parseScreenState(QStringLiteral("  mWakefulness=Awake\nDisplay Power: state=ON\n    mShowingLockscreen=false\n"));
        QVERIFY(s.known);
        QVERIFY(s.awake);
        QVERIFY(s.displayOn);
        QVERIFY(!s.locked);
        s = parseScreenState(QStringLiteral("  mWakefulness=Asleep\nDisplay Power: state=OFF\n    isKeyguardShowing=true\n"));
        QVERIFY(!s.awake);
        QVERIFY(!s.displayOn);
        QVERIFY(s.locked);
        QVERIFY(!parseScreenState(QStringLiteral("garbage")).known);
    }

    void arp()
    {
        const QString out = QStringLiteral(
            "Interface: 192.168.100.180 --- 0xc\n"
            "  Internet Address      Physical Address      Type\n"
            "  192.168.100.1         a4-2b-b0-11-22-33     dynamic\n"
            "  192.168.100.13        e8-11-22-33-44-55     dynamic\n"
            "  192.168.100.255       ff-ff-ff-ff-ff-ff     static\n"
            "  224.0.0.22            01-00-5e-00-00-16     static\n");
        const QStringList hosts = parseArpNeighbours(out);
        QCOMPARE(hosts, (QStringList{ QStringLiteral("192.168.100.1"), QStringLiteral("192.168.100.13") }));
    }

    void misc()
    {
        QCOMPARE(parseWmSize(QStringLiteral("Physical size: 1440x2960\nOverride size: 1080x2220\n")), QStringLiteral("1080x2220"));
        QCOMPARE(parseWmSize(QStringLiteral("Physical size: 1440x2960\n")), QStringLiteral("1440x2960"));
        QCOMPARE(parseDfFreeKb(QStringLiteral("Filesystem     1K-blocks    Used Available Use% Mounted on\n/dev/block/dm-0  57000000 30000000  26500000  54% /data\n")), qint64(26500000));
        QCOMPARE(parseWifiRssi(QStringLiteral("mWifiInfo SSID: \"Farm\", BSSID: aa, RSSI: -56, Link speed: 72Mbps")), -56);
        QCOMPARE(parseWifiRssi(QStringLiteral("nothing")), 0);
        QVERIFY(parseConnectSuccess(QStringLiteral("connected to 192.168.100.13:5555\n")));
        QVERIFY(parseConnectSuccess(QStringLiteral("already connected to 192.168.100.13:5555")));
        QVERIFY(!parseConnectSuccess(QStringLiteral("failed to connect to '192.168.100.13:5555': Connection refused")));
        QVERIFY(!parseConnectSuccess(QStringLiteral("cannot connect to 192.168.100.13:5555: No connection could be made")));
        QCOMPARE(parseWlanIp(QStringLiteral("24: wlan0    inet 192.168.100.13/24 brd 192.168.100.255 scope global wlan0")), QStringLiteral("192.168.100.13"));
        QCOMPARE(parseWlanIp(QStringLiteral("wlan0     Link encap:Ethernet\n          inet addr:192.168.1.7  Bcast:192.168.1.255  Mask:255.255.255.0")), QStringLiteral("192.168.1.7"));
    }

    void quoting()
    {
        QCOMPARE(shellQuote(QStringLiteral("simple-name_1.txt")), QStringLiteral("simple-name_1.txt"));
        QCOMPARE(shellQuote(QStringLiteral("/sdcard/Download/my photo.jpg")), QStringLiteral("'/sdcard/Download/my photo.jpg'"));
        QCOMPARE(shellQuote(QStringLiteral("it's")), QStringLiteral("'it'\\''s'"));
        QCOMPARE(shellQuote(QString()), QStringLiteral("''"));
        QCOMPARE(shellQuote(QStringLiteral("$(rm -rf /)")), QStringLiteral("'$(rm -rf /)'"));
        QCOMPARE(shellJoin({ QStringLiteral("rm"), QStringLiteral("-f"), QStringLiteral("a b") }), QStringLiteral("rm -f 'a b'"));
        QCOMPARE(inputTextEscape(QStringLiteral("hello world (1)")), QStringLiteral("hello%sworld%s\\(1\\)"));
    }
};

QTEST_APPLESS_MAIN(TestAdbParsers)
#include "tst_adbparsers.moc"
