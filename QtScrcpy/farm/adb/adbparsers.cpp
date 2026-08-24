#include "adbparsers.h"

#include <QRegularExpression>

#include "../core/ipv4.h"

namespace farm {
namespace adb {

QList<AdbDeviceInfo> parseDevicesList(const QString &stdOut)
{
    QList<AdbDeviceInfo> list;
    const QStringList lines = stdOut.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    for (const QString &raw : lines) {
        const QString line = raw.trimmed();
        if (line.isEmpty() || line.startsWith(QLatin1String("List of devices")) || line.startsWith(QLatin1Char('*'))) {
            continue;
        }
        const QStringList tokens = line.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
        if (tokens.size() < 2) {
            continue;
        }
        AdbDeviceInfo info;
        info.serial = tokens.at(0);
        info.state = tokens.at(1);
        // "no permissions" is two words; normalise it.
        if (info.state == QLatin1String("no") && tokens.size() > 2 && tokens.at(2).startsWith(QLatin1String("permissions"))) {
            info.state = QStringLiteral("no permissions");
        }
        for (int i = 2; i < tokens.size(); ++i) {
            const QString &t = tokens.at(i);
            const int colon = t.indexOf(QLatin1Char(':'));
            if (colon <= 0) {
                continue;
            }
            const QString key = t.left(colon);
            const QString value = t.mid(colon + 1);
            if (key == QLatin1String("product")) {
                info.product = value;
            } else if (key == QLatin1String("model")) {
                info.model = value;
            } else if (key == QLatin1String("device")) {
                info.device = value;
            } else if (key == QLatin1String("transport_id")) {
                info.transportId = value;
            }
        }
        info.isTcp = ipv4::isTcpEndpoint(info.serial);
        list.append(info);
    }
    return list;
}

QList<MdnsService> parseMdnsServices(const QString &stdOut)
{
    QList<MdnsService> list;
    const QStringList lines = stdOut.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    for (const QString &raw : lines) {
        const QString line = raw.trimmed();
        if (line.startsWith(QLatin1String("List of")) || line.isEmpty()) {
            continue;
        }
        const QStringList tokens = line.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
        if (tokens.size() < 3) {
            continue;
        }
        MdnsService s;
        s.name = tokens.at(0);
        s.type = tokens.at(1);
        s.address = tokens.at(2);
        list.append(s);
    }
    return list;
}

QHash<QString, QString> parseGetProp(const QString &stdOut)
{
    QHash<QString, QString> props;
    static const QRegularExpression re(QStringLiteral("^\\[([^\\]]+)\\]:\\s*\\[(.*)\\]\\s*$"));
    const QStringList lines = stdOut.split(QLatin1Char('\n'));
    for (const QString &raw : lines) {
        const QRegularExpressionMatch m = re.match(raw.trimmed());
        if (m.hasMatch()) {
            props.insert(m.captured(1), m.captured(2));
        }
    }
    return props;
}

BatteryInfo parseBattery(const QString &stdOut)
{
    BatteryInfo info;
    const QStringList lines = stdOut.split(QLatin1Char('\n'));
    for (const QString &raw : lines) {
        const QString line = raw.trimmed();
        const int colon = line.indexOf(QLatin1Char(':'));
        if (colon <= 0) {
            continue;
        }
        const QString key = line.left(colon).trimmed();
        const QString value = line.mid(colon + 1).trimmed();
        if (key == QLatin1String("level")) {
            info.level = value.toInt();
        } else if (key == QLatin1String("status")) {
            info.status = value.toInt();
        } else if (key == QLatin1String("AC powered")) {
            info.acPowered = value == QLatin1String("true");
        } else if (key == QLatin1String("USB powered")) {
            info.usbPowered = value == QLatin1String("true");
        } else if (key == QLatin1String("Wireless powered")) {
            info.wirelessPowered = value == QLatin1String("true");
        } else if (key == QLatin1String("temperature")) {
            info.temperatureC = value.toInt() / 10.0;
        } else if (key == QLatin1String("voltage")) {
            info.voltageMv = value.toInt();
        }
    }
    return info;
}

QList<PackageInfo> parsePackages(const QString &stdOut)
{
    QList<PackageInfo> list;
    const QStringList lines = stdOut.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    for (const QString &raw : lines) {
        QString line = raw.trimmed();
        if (!line.startsWith(QLatin1String("package:"))) {
            continue;
        }
        line = line.mid(8);
        PackageInfo p;
        // With -f: "package:/data/app/x.apk=com.example"
        const int eq = line.lastIndexOf(QLatin1Char('='));
        if (eq > 0 && line.startsWith(QLatin1Char('/'))) {
            p.apkPath = line.left(eq);
            p.name = line.mid(eq + 1);
        } else {
            p.name = line;
        }
        list.append(p);
    }
    return list;
}

QList<RemoteEntry> parseLsLa(const QString &stdOut)
{
    QList<RemoteEntry> list;
    // toybox: "drwxrwx--x 4 root sdcard_rw 4096 2026-08-24 10:11 Download"
    // toolbox: "drwxrwx--x root sdcard_rw 2026-08-24 10:11 Download"
    static const QRegularExpression re(
        QStringLiteral("^([\\-dlcbps][rwxsStT\\-]{9})\\s+(?:\\d+\\s+)?(\\S+)\\s+(\\S+)\\s+(?:(\\d+)\\s+)?(\\d{4}-\\d{2}-\\d{2})\\s+(\\d{2}:\\d{2})\\s+(.+)$"));
    const QStringList lines = stdOut.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    for (const QString &raw : lines) {
        const QString line = raw.trimmed();
        if (line.startsWith(QLatin1String("total "))) {
            continue;
        }
        const QRegularExpressionMatch m = re.match(line);
        if (!m.hasMatch()) {
            continue;
        }
        RemoteEntry e;
        e.permissions = m.captured(1);
        e.isDir = e.permissions.startsWith(QLatin1Char('d'));
        e.isLink = e.permissions.startsWith(QLatin1Char('l'));
        e.size = m.captured(4).toLongLong();
        e.modified = m.captured(5) + QLatin1Char(' ') + m.captured(6);
        e.name = m.captured(7);
        if (e.isLink) {
            const int arrow = e.name.indexOf(QLatin1String(" -> "));
            if (arrow > 0) {
                e.name = e.name.left(arrow);
            }
        }
        if (e.name == QLatin1String(".") || e.name == QLatin1String("..")) {
            continue;
        }
        list.append(e);
    }
    return list;
}

ScreenState parseScreenState(const QString &stdOut)
{
    ScreenState s;
    static const QRegularExpression wake(QStringLiteral("mWakefulness=(\\w+)"));
    static const QRegularExpression display(QStringLiteral("Display Power: state=(\\w+)"));
    static const QRegularExpression keyguard(
        QStringLiteral("(?:mShowingLockscreen|isKeyguardShowing|mKeyguardShowing|showing|mDreamingLockscreen|mIsShowing)=(true|false)"));
    const QRegularExpressionMatch w = wake.match(stdOut);
    if (w.hasMatch()) {
        s.known = true;
        s.awake = w.captured(1).compare(QLatin1String("Awake"), Qt::CaseInsensitive) == 0;
    }
    const QRegularExpressionMatch d = display.match(stdOut);
    if (d.hasMatch()) {
        s.known = true;
        s.displayOn = d.captured(1).compare(QLatin1String("ON"), Qt::CaseInsensitive) == 0;
    }
    if (!d.hasMatch() && w.hasMatch()) {
        s.displayOn = s.awake;
    }
    QRegularExpressionMatchIterator it = keyguard.globalMatch(stdOut);
    while (it.hasNext()) {
        const QRegularExpressionMatch k = it.next();
        if (k.captured(1) == QLatin1String("true")) {
            s.locked = true;
        }
    }
    return s;
}

QStringList parseArpNeighbours(const QString &stdOut)
{
    QStringList list;
    static const QRegularExpression re(QStringLiteral("^\\s*(\\d{1,3}(?:\\.\\d{1,3}){3})\\s+([0-9a-fA-F]{2}(?:-[0-9a-fA-F]{2}){5})\\s+(\\w+)"));
    const QStringList lines = stdOut.split(QLatin1Char('\n'));
    for (const QString &raw : lines) {
        const QRegularExpressionMatch m = re.match(raw);
        if (!m.hasMatch()) {
            continue;
        }
        const QString ip = m.captured(1);
        const QString mac = m.captured(2).toLower();
        if (mac == QLatin1String("ff-ff-ff-ff-ff-ff") || ip.endsWith(QLatin1String(".255")) || ip.startsWith(QLatin1String("224."))
            || ip.startsWith(QLatin1String("239."))) {
            continue;
        }
        if (!list.contains(ip)) {
            list.append(ip);
        }
    }
    return list;
}

QString parseWmSize(const QString &stdOut)
{
    static const QRegularExpression overrideRe(QStringLiteral("Override size:\\s*(\\d+x\\d+)"));
    static const QRegularExpression physicalRe(QStringLiteral("Physical size:\\s*(\\d+x\\d+)"));
    QRegularExpressionMatch m = overrideRe.match(stdOut);
    if (m.hasMatch()) {
        return m.captured(1);
    }
    m = physicalRe.match(stdOut);
    return m.hasMatch() ? m.captured(1) : QString();
}

qint64 parseDfFreeKb(const QString &stdOut)
{
    const QStringList lines = stdOut.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    for (int i = lines.size() - 1; i >= 0; --i) {
        const QStringList tokens = lines.at(i).trimmed().split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
        // Filesystem 1K-blocks Used Available Use% Mounted on
        if (tokens.size() >= 4 && tokens.at(1).at(0).isDigit()) {
            bool ok = false;
            const qint64 avail = tokens.at(3).toLongLong(&ok);
            if (ok) {
                return avail;
            }
        }
    }
    return -1;
}

int parseWifiRssi(const QString &stdOut)
{
    static const QRegularExpression re(QStringLiteral("(?:RSSI|mRssi|rssi)[:=]\\s*(-?\\d+)"));
    const QRegularExpressionMatch m = re.match(stdOut);
    if (!m.hasMatch()) {
        return 0;
    }
    const int rssi = m.captured(1).toInt();
    return (rssi < 0 && rssi > -127) ? rssi : 0;
}

bool parseConnectSuccess(const QString &stdOut)
{
    const QString s = stdOut.trimmed();
    return s.startsWith(QLatin1String("connected to")) || s.startsWith(QLatin1String("already connected"));
}

QString parseWlanIp(const QString &stdOut)
{
    static const QRegularExpression ipAddr(QStringLiteral("wlan\\d\\s+inet\\s+(\\d{1,3}(?:\\.\\d{1,3}){3})"));
    QRegularExpressionMatch m = ipAddr.match(stdOut);
    if (m.hasMatch()) {
        return m.captured(1);
    }
    static const QRegularExpression ifconfig(QStringLiteral("inet addr:(\\d{1,3}(?:\\.\\d{1,3}){3})"));
    m = ifconfig.match(stdOut);
    if (m.hasMatch()) {
        return m.captured(1);
    }
    static const QRegularExpression anyInet(QStringLiteral("inet\\s+(\\d{1,3}(?:\\.\\d{1,3}){3})"));
    QRegularExpressionMatchIterator it = anyInet.globalMatch(stdOut);
    while (it.hasNext()) {
        const QString ip = it.next().captured(1);
        if (!ip.startsWith(QLatin1String("127."))) {
            return ip;
        }
    }
    return QString();
}

} // namespace adb
} // namespace farm
