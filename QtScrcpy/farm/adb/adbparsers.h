#ifndef FARM_ADB_ADBPARSERS_H
#define FARM_ADB_ADBPARSERS_H

#include <QHash>
#include <QList>
#include <QMetaType>
#include <QString>
#include <QStringList>

namespace farm {
namespace adb {

struct AdbDeviceInfo
{
    QString serial;      // "192.168.100.13:5555" or a USB serial
    QString state;       // device / offline / unauthorized / recovery / sideload / no permissions / ...
    QString product;
    QString model;       // "SM_G9500" (adb replaces spaces with '_')
    QString device;
    QString transportId;
    bool isTcp = false;
    bool isOnline() const { return state == QLatin1String("device"); }
};

/// Parse `adb devices -l` (also accepts plain `adb devices`).
QList<AdbDeviceInfo> parseDevicesList(const QString &stdOut);

struct MdnsService
{
    QString name;        // adb-XXXX-abcdef
    QString type;        // _adb-tls-connect._tcp / _adb._tcp
    QString address;     // ip:port
};
/// Parse `adb mdns services`.
QList<MdnsService> parseMdnsServices(const QString &stdOut);

/// Parse `getprop` output ("[key]: [value]") into a map.
QHash<QString, QString> parseGetProp(const QString &stdOut);

struct BatteryInfo
{
    int level = -1;             // percent
    int status = 0;             // 2 charging, 3 discharging, 4 not charging, 5 full
    bool acPowered = false;
    bool usbPowered = false;
    bool wirelessPowered = false;
    double temperatureC = -1;   // dumpsys reports tenths of a degree
    int voltageMv = -1;
    bool charging() const { return status == 2 || status == 5 || acPowered || usbPowered || wirelessPowered; }
};
/// Parse `dumpsys battery`.
BatteryInfo parseBattery(const QString &stdOut);

struct PackageInfo
{
    QString name;
    QString apkPath;    // from `pm list packages -f`
};
/// Parse `pm list packages [-f]`.
QList<PackageInfo> parsePackages(const QString &stdOut);

struct RemoteEntry
{
    QString name;
    bool isDir = false;
    bool isLink = false;
    qint64 size = 0;
    QString permissions;
    QString modified;    // "2026-08-24 10:11"
};
/// Parse `ls -la` output from toybox/toolbox `ls`.
QList<RemoteEntry> parseLsLa(const QString &stdOut);

struct ScreenState
{
    bool known = false;
    bool awake = false;      // mWakefulness=Awake
    bool displayOn = false;  // Display Power: state=ON
    bool locked = false;     // keyguard showing
};
/// Parse the combined `dumpsys power` + `dumpsys window policy` probe.
ScreenState parseScreenState(const QString &stdOut);

/// Parse Windows `arp -a` into a list of IPv4 neighbours (all interfaces).
QStringList parseArpNeighbours(const QString &stdOut);

/// Parse `wm size` -> "1080x2220" (override size wins over physical).
QString parseWmSize(const QString &stdOut);

/// Parse `df -k /data` style output -> free kilobytes, -1 if unknown.
qint64 parseDfFreeKb(const QString &stdOut);

/// Parse the RSSI from `dumpsys wifi` ("RSSI: -56" / "mRssi=-56"), 0 if unknown.
int parseWifiRssi(const QString &stdOut);

/// Parse `adb connect` output: true when the endpoint is (already) connected.
bool parseConnectSuccess(const QString &stdOut);

/// Extract the wlan0 IPv4 from `ip -o addr` / `ifconfig wlan0`, empty if none.
QString parseWlanIp(const QString &stdOut);

} // namespace adb
} // namespace farm

Q_DECLARE_METATYPE(farm::adb::AdbDeviceInfo)

#endif // FARM_ADB_ADBPARSERS_H
