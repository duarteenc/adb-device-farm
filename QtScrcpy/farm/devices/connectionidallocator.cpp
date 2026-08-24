#include "connectionidallocator.h"

#include <QHostAddress>
#include <QRandomGenerator>
#include <QTcpServer>

namespace farm {

ConnectionIdAllocator &ConnectionIdAllocator::instance()
{
    static ConnectionIdAllocator allocator;
    return allocator;
}

bool ConnectionIdAllocator::portFree(quint16 port)
{
    QTcpServer probe;
    const bool ok = probe.listen(QHostAddress::LocalHost, port);
    probe.close();
    return ok;
}

ConnectionIdAllocator::Lease ConnectionIdAllocator::acquire(const QString &owner)
{
    QMutexLocker lock(&m_mutex);
    if (m_leases.contains(owner)) {
        return m_leases.value(owner);
    }
    Lease lease;
    // scid: random 31-bit, non-zero, unique among live sessions.
    for (int attempt = 0; attempt < 64; ++attempt) {
        const quint32 candidate = QRandomGenerator::global()->bounded(1u, 0x7FFFFFFFu);
        if (!m_scids.contains(candidate)) {
            lease.scid = candidate;
            break;
        }
    }
    if (lease.scid == 0) {
        return Lease();
    }
    // port: round-robin over the pool so a just-released port isn't immediately
    // reused while the OS still has it in TIME_WAIT.
    for (quint32 i = 0; i < m_portCount; ++i) {
        const quint32 offset = (static_cast<quint32>(m_nextPortHint - m_firstPort) + i) % m_portCount;
        const quint16 port = static_cast<quint16>(m_firstPort + offset);
        if (m_ports.contains(port)) {
            continue;
        }
        if (m_probeBinding && !portFree(port)) {
            continue;
        }
        lease.localPort = port;
        m_nextPortHint = static_cast<quint16>(m_firstPort + ((offset + 1) % m_portCount));
        break;
    }
    if (lease.localPort == 0) {
        return Lease();
    }
    m_scids.insert(lease.scid);
    m_ports.insert(lease.localPort);
    m_leases.insert(owner, lease);
    return lease;
}

void ConnectionIdAllocator::release(const QString &owner)
{
    QMutexLocker lock(&m_mutex);
    const Lease lease = m_leases.take(owner);
    if (lease.valid()) {
        m_scids.remove(lease.scid);
        m_ports.remove(lease.localPort);
    }
}

bool ConnectionIdAllocator::isPortInUse(quint16 port) const
{
    QMutexLocker lock(&m_mutex);
    return m_ports.contains(port);
}

bool ConnectionIdAllocator::isScidInUse(quint32 scid) const
{
    QMutexLocker lock(&m_mutex);
    return m_scids.contains(scid);
}

int ConnectionIdAllocator::inUseCount() const
{
    QMutexLocker lock(&m_mutex);
    return static_cast<int>(m_leases.size());
}

QHash<QString, ConnectionIdAllocator::Lease> ConnectionIdAllocator::owners() const
{
    QMutexLocker lock(&m_mutex);
    return m_leases;
}

void ConnectionIdAllocator::setPortRange(quint16 first, quint16 count)
{
    QMutexLocker lock(&m_mutex);
    m_firstPort = first;
    m_portCount = count == 0 ? 1 : count;
    m_nextPortHint = first;
}

} // namespace farm
