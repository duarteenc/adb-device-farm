#ifndef FARM_CORE_IPV4_H
#define FARM_CORE_IPV4_H

#include <QString>
#include <QStringList>
#include <QtGlobal>

namespace farm {

/**
 * Small, dependency-free IPv4 helpers shared by discovery, sorting and the UI.
 * All functions are pure and covered by tests/tst_ipv4.cpp.
 */
namespace ipv4 {

/// Parse "a.b.c.d" into a host-order 32-bit value. Returns false on any malformed octet.
bool parse(const QString &text, quint32 &out);

/// Format a host-order 32-bit value as "a.b.c.d".
QString toString(quint32 value);

/// True if `text` is a syntactically valid dotted quad.
bool isValid(const QString &text);

/// Extract the host part of "ip:port" (or the whole string when there is no port).
QString hostOf(const QString &serialOrEndpoint);

/// Extract the port of "ip:port", or `fallback` when absent/invalid.
quint16 portOf(const QString &serialOrEndpoint, quint16 fallback = 5555);

/// True when the serial looks like an ADB-over-TCP endpoint ("ip:port").
bool isTcpEndpoint(const QString &serial);

struct Range
{
    quint32 first = 0;    // inclusive, host order
    quint32 last = 0;     // inclusive, host order
    bool isValid() const { return last >= first; }
    quint32 count() const { return isValid() ? (last - first + 1) : 0; }
};

/**
 * Parse a CIDR block ("192.168.100.0/24") into the USABLE host range: the network
 * and broadcast addresses are excluded for prefixes shorter than /31 (so /24 yields
 * .1 - .254). /32 yields the single host; /31 yields both addresses (RFC 3021).
 */
bool parseCidr(const QString &cidr, Range &out);

/**
 * Parse a human range: "192.168.1.10-20", "192.168.1.10-192.168.1.20",
 * a single IP, or a CIDR. Never returns .0/.255 of a /24-style block when given
 * a CIDR; explicit ranges are returned as typed.
 */
bool parseRange(const QString &text, Range &out);

/// Expand a range to a list of dotted quads (capped at `maxHosts`).
QStringList expand(const Range &range, int maxHosts = 4096);

/**
 * Numeric ordering key for sorting serials/endpoints: TCP endpoints sort by their
 * 32-bit address (so .9 < .10), USB serials sort after them alphabetically.
 */
bool lessThan(const QString &a, const QString &b);

/// Last octet of a TCP endpoint (0 when not an IP) — used for automatic numbering.
int lastOctet(const QString &serialOrEndpoint);

} // namespace ipv4
} // namespace farm

#endif // FARM_CORE_IPV4_H
