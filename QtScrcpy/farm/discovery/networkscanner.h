#ifndef FARM_DISCOVERY_NETWORKSCANNER_H
#define FARM_DISCOVERY_NETWORKSCANNER_H

#include <QElapsedTimer>
#include <QList>
#include <QObject>
#include <QSet>
#include <QStringList>

#include "../core/ipv4.h"
#include "../core/taskexecutor.h"

class QThread;
class QTcpSocket;

namespace farm {

/**
 * Asynchronous, bounded TCP port probe of an IPv4 range (the "is anything
 * listening on 5555?" step of LAN discovery). Runs on its own thread with an
 * event loop and at most `concurrency` sockets in flight; a whole /24 takes
 * ~2-4 s. Never blocks the caller. Hosts in `priorityHosts` (known devices,
 * ARP neighbours) are probed first so they appear immediately.
 */
class NetworkScanner : public QObject
{
    Q_OBJECT
public:
    struct Options
    {
        ipv4::Range range;
        quint16 port = 5555;
        int concurrency = 64;
        int timeoutMs = 800;
        QStringList priorityHosts;
        QStringList excludeHosts;    // e.g. the PC's own addresses
    };

    explicit NetworkScanner(QObject *parent = nullptr);
    ~NetworkScanner() override;

    void start(const Options &options, CancellationToken token = CancellationToken());
    void cancel();
    bool isRunning() const { return m_running; }

signals:
    void hostFound(const QString &host);
    void progress(int done, int total);
    void finished(const QStringList &found, qint64 elapsedMs, bool cancelled);

private:
    class Worker;
    QThread *m_thread = nullptr;
    Worker *m_worker = nullptr;
    bool m_running = false;
};

} // namespace farm

#endif // FARM_DISCOVERY_NETWORKSCANNER_H
