#ifndef FARM_DEVICES_CONNECTIONIDALLOCATOR_H
#define FARM_DEVICES_CONNECTIONIDALLOCATOR_H

#include <QHash>
#include <QMutex>
#include <QSet>
#include <QString>

namespace farm {

/**
 * Hands out the two values every simultaneous scrcpy session must have unique:
 *
 *  - `scid`   : 31-bit id appended to the device-side local socket name
 *               (`scrcpy_XXXXXXXX`), so several sessions on one phone don't collide.
 *  - localPort: the PC-side port of the `adb reverse`/`forward` tunnel.
 *
 * Thread-safe; ids/ports are released on disconnect and never handed out twice
 * while in use. `owners()` exposes the current map for diagnostics.
 */
class ConnectionIdAllocator
{
public:
    static ConnectionIdAllocator &instance();

    struct Lease
    {
        quint32 scid = 0;
        quint16 localPort = 0;
        bool valid() const { return scid != 0 && localPort != 0; }
    };

    /// Reserve a unique scid + port for `owner` (a device id). Returns an invalid
    /// lease when the port pool is exhausted.
    Lease acquire(const QString &owner);
    void release(const QString &owner);
    bool isPortInUse(quint16 port) const;
    bool isScidInUse(quint32 scid) const;
    int inUseCount() const;
    QHash<QString, Lease> owners() const;

    void setPortRange(quint16 first, quint16 count);
    quint16 firstPort() const { return m_firstPort; }
    quint16 portCount() const { return m_portCount; }

    /// Optional: probe that the port can actually be bound on localhost (skips
    /// ports another process holds). Default on; tests turn it off.
    void setProbeBinding(bool probe) { m_probeBinding = probe; }

private:
    ConnectionIdAllocator() = default;
    static bool portFree(quint16 port);

    mutable QMutex m_mutex;
    QHash<QString, Lease> m_leases;
    QSet<quint16> m_ports;
    QSet<quint32> m_scids;
    quint16 m_firstPort = 27183;
    quint16 m_portCount = 2000;
    quint16 m_nextPortHint = 27183;
    bool m_probeBinding = true;
};

} // namespace farm

#endif // FARM_DEVICES_CONNECTIONIDALLOCATOR_H
