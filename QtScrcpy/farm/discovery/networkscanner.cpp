#include "networkscanner.h"

#include <algorithm>
#include <memory>

#include <QHostAddress>
#include <QTcpSocket>
#include <QThread>
#include <QTimer>

namespace farm {

// Lives on the scanner thread; owns the sockets.
class NetworkScanner::Worker : public QObject
{
public:
    explicit Worker(NetworkScanner *owner)
        : m_owner(owner)
    {
    }

    void begin(const Options &options, CancellationToken token)
    {
        m_token = token;
        m_found.clear();
        m_queue.clear();
        m_done = 0;
        m_inFlight = 0;
        m_cancelled = false;
        QSet<QString> seen;
        for (const QString &h : options.priorityHosts) {
            if (!seen.contains(h) && !options.excludeHosts.contains(h)) {
                seen.insert(h);
                m_queue.append(h);
            }
        }
        const QStringList hosts = ipv4::expand(options.range, 65536);
        for (const QString &h : hosts) {
            if (!seen.contains(h) && !options.excludeHosts.contains(h)) {
                seen.insert(h);
                m_queue.append(h);
            }
        }
        m_total = static_cast<int>(m_queue.size());
        m_port = options.port;
        m_timeoutMs = std::max(100, options.timeoutMs);
        m_concurrency = std::clamp(options.concurrency, 1, 256);
        m_timer.start();
        emit m_owner->progress(0, m_total);
        if (m_total == 0) {
            finish();
            return;
        }
        pump();
    }

    void cancelNow()
    {
        m_cancelled = true;
        m_queue.clear();
        if (m_inFlight == 0) {
            finish();
        }
    }

private:
    void pump()
    {
        while (m_inFlight < m_concurrency && !m_queue.isEmpty() && !m_token.isCancelled()) {
            const QString host = m_queue.takeFirst();
            probe(host);
        }
        if (m_token.isCancelled() && !m_cancelled) {
            m_cancelled = true;
            m_queue.clear();
        }
        if (m_inFlight == 0 && (m_queue.isEmpty() || m_cancelled)) {
            finish();
        }
    }

    void probe(const QString &host)
    {
        ++m_inFlight;
        auto *socket = new QTcpSocket(this);
        auto *timer = new QTimer(socket);
        timer->setSingleShot(true);
        auto settled = std::make_shared<bool>(false);
        auto complete = [this, socket, timer, settled, host](bool open) {
            if (*settled) {
                return;
            }
            *settled = true;
            timer->stop();
            socket->disconnect();
            socket->abort();
            socket->deleteLater();
            --m_inFlight;
            ++m_done;
            if (open) {
                m_found.append(host);
                emit m_owner->hostFound(host);
            }
            if ((m_done & 7) == 0 || m_done == m_total) {
                emit m_owner->progress(m_done, m_total);
            }
            pump();
        };
        connect(socket, &QTcpSocket::connected, this, [complete]() { complete(true); });
        connect(socket, &QTcpSocket::errorOccurred, this, [complete](QAbstractSocket::SocketError) { complete(false); });
        connect(timer, &QTimer::timeout, this, [complete]() { complete(false); });
        timer->start(m_timeoutMs);
        socket->connectToHost(QHostAddress(host), m_port);
    }

    void finish()
    {
        const QStringList found = m_found;
        const qint64 ms = m_timer.elapsed();
        const bool cancelled = m_cancelled || m_token.isCancelled();
        m_found.clear();
        emit m_owner->progress(m_done, m_total);
        emit m_owner->finished(found, ms, cancelled);
    }

    NetworkScanner *m_owner;
    CancellationToken m_token;
    QStringList m_queue;
    QStringList m_found;
    QElapsedTimer m_timer;
    int m_total = 0;
    int m_done = 0;
    int m_inFlight = 0;
    int m_concurrency = 64;
    int m_timeoutMs = 800;
    quint16 m_port = 5555;
    bool m_cancelled = false;
};

NetworkScanner::NetworkScanner(QObject *parent)
    : QObject(parent)
{
    m_thread = new QThread(this);
    m_thread->setObjectName(QStringLiteral("net-scanner"));
    m_worker = new Worker(this);
    m_worker->moveToThread(m_thread);
    m_thread->start();
    connect(this, &NetworkScanner::finished, this, [this]() { m_running = false; });
}

NetworkScanner::~NetworkScanner()
{
    QMetaObject::invokeMethod(m_worker, [this]() { m_worker->deleteLater(); }, Qt::BlockingQueuedConnection);
    m_thread->quit();
    m_thread->wait(2000);
}

void NetworkScanner::start(const Options &options, CancellationToken token)
{
    if (m_running) {
        return;
    }
    m_running = true;
    QMetaObject::invokeMethod(m_worker, [this, options, token]() { m_worker->begin(options, token); }, Qt::QueuedConnection);
}

void NetworkScanner::cancel()
{
    if (!m_running) {
        return;
    }
    QMetaObject::invokeMethod(m_worker, [this]() { m_worker->cancelNow(); }, Qt::QueuedConnection);
}

} // namespace farm
