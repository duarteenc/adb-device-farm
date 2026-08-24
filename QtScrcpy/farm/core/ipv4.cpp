#include "ipv4.h"

#include <algorithm>

namespace farm {
namespace ipv4 {

bool parse(const QString &text, quint32 &out)
{
    const QStringList parts = text.trimmed().split(QLatin1Char('.'));
    if (parts.size() != 4) {
        return false;
    }
    quint32 value = 0;
    for (const QString &part : parts) {
        if (part.isEmpty() || part.size() > 3) {
            return false;
        }
        bool ok = false;
        const int octet = part.toInt(&ok, 10);
        if (!ok || octet < 0 || octet > 255) {
            return false;
        }
        value = (value << 8) | static_cast<quint32>(octet);
    }
    out = value;
    return true;
}

QString toString(quint32 value)
{
    return QStringLiteral("%1.%2.%3.%4")
        .arg((value >> 24) & 0xFFu)
        .arg((value >> 16) & 0xFFu)
        .arg((value >> 8) & 0xFFu)
        .arg(value & 0xFFu);
}

bool isValid(const QString &text)
{
    quint32 dummy = 0;
    return parse(text, dummy);
}

QString hostOf(const QString &serialOrEndpoint)
{
    const int colon = serialOrEndpoint.indexOf(QLatin1Char(':'));
    return colon < 0 ? serialOrEndpoint : serialOrEndpoint.left(colon);
}

quint16 portOf(const QString &serialOrEndpoint, quint16 fallback)
{
    const int colon = serialOrEndpoint.indexOf(QLatin1Char(':'));
    if (colon < 0) {
        return fallback;
    }
    bool ok = false;
    const int port = serialOrEndpoint.mid(colon + 1).toInt(&ok);
    if (!ok || port <= 0 || port > 65535) {
        return fallback;
    }
    return static_cast<quint16>(port);
}

bool isTcpEndpoint(const QString &serial)
{
    return isValid(hostOf(serial)) && serial.contains(QLatin1Char(':'));
}

bool parseCidr(const QString &cidr, Range &out)
{
    const int slash = cidr.indexOf(QLatin1Char('/'));
    if (slash < 0) {
        return false;
    }
    quint32 base = 0;
    if (!parse(cidr.left(slash), base)) {
        return false;
    }
    bool ok = false;
    const int prefix = cidr.mid(slash + 1).trimmed().toInt(&ok);
    if (!ok || prefix < 0 || prefix > 32) {
        return false;
    }
    const quint32 mask = prefix == 0 ? 0u : (0xFFFFFFFFu << (32 - prefix));
    const quint32 network = base & mask;
    const quint32 broadcast = network | ~mask;
    if (prefix >= 31) {
        out.first = network;
        out.last = broadcast;
    } else {
        out.first = network + 1;
        out.last = broadcast - 1;
    }
    return out.isValid();
}

bool parseRange(const QString &input, Range &out)
{
    const QString text = input.trimmed();
    if (text.isEmpty()) {
        return false;
    }
    if (text.contains(QLatin1Char('/'))) {
        return parseCidr(text, out);
    }
    const int dash = text.indexOf(QLatin1Char('-'));
    if (dash < 0) {
        quint32 single = 0;
        if (!parse(hostOf(text), single)) {
            return false;
        }
        out.first = out.last = single;
        return true;
    }
    const QString left = hostOf(text.left(dash).trimmed());
    const QString right = hostOf(text.mid(dash + 1).trimmed());
    quint32 first = 0;
    if (!parse(left, first)) {
        return false;
    }
    quint32 last = 0;
    if (right.contains(QLatin1Char('.'))) {
        if (!parse(right, last)) {
            return false;
        }
    } else {
        bool ok = false;
        const int octet = right.toInt(&ok);
        if (!ok || octet < 0 || octet > 255) {
            return false;
        }
        last = (first & 0xFFFFFF00u) | static_cast<quint32>(octet);
    }
    if (last < first) {
        return false;
    }
    out.first = first;
    out.last = last;
    return true;
}

QStringList expand(const Range &range, int maxHosts)
{
    QStringList list;
    if (!range.isValid() || maxHosts <= 0) {
        return list;
    }
    const quint32 count = std::min<quint32>(range.count(), static_cast<quint32>(maxHosts));
    list.reserve(static_cast<qsizetype>(count));
    for (quint32 i = 0; i < count; ++i) {
        list.append(toString(range.first + i));
    }
    return list;
}

bool lessThan(const QString &a, const QString &b)
{
    quint32 ia = 0;
    quint32 ib = 0;
    const bool aIp = parse(hostOf(a), ia);
    const bool bIp = parse(hostOf(b), ib);
    if (aIp && bIp) {
        if (ia != ib) {
            return ia < ib;
        }
        return portOf(a) < portOf(b);
    }
    if (aIp != bIp) {
        return aIp;    // IPs first, then USB serials
    }
    return QString::compare(a, b, Qt::CaseInsensitive) < 0;
}

int lastOctet(const QString &serialOrEndpoint)
{
    quint32 value = 0;
    if (!parse(hostOf(serialOrEndpoint), value)) {
        return 0;
    }
    return static_cast<int>(value & 0xFFu);
}

} // namespace ipv4
} // namespace farm
