#ifndef FARM_DISCOVERY_DEVICEDISCOVERYSERVICE_H
#define FARM_DISCOVERY_DEVICEDISCOVERYSERVICE_H

#include <QDateTime>
#include <QHash>
#include <QObject>
#include <QSet>
#include <QStringList>
#include <QTimer>

#include "../adb/adbparsers.h"
#include "../core/taskexecutor.h"
#include "networkscanner.h"

namespace farm {

/**
 * Multi-method, continuous device discovery (never blocks the GUI):
 *
 *   A. `adb devices -l`             quick refresh every few seconds (USB + already-connected TCP)
 *   B. `adb mdns services`          wireless-debugging advertisements (when the adb host supports it)
 *   C. known-device registry        previously seen ip:port endpoints are retried first
 *   D. subnet TCP probe             192.168.100.1-254 (configurable CIDR), bounded parallel connects
 *   E. ARP neighbour table          hosts the OS already talked to are probed first
 *   F. last-known IPs               folded into C
 *
 * Every candidate that answers on the ADB port gets a bounded, timed `adb connect`.
 * The result flows into DeviceRegistry (Discovered -> AdbOnline / Unauthorized /
 * Offline) and DeviceService decides whether to auto-mirror.
 *
 * Adaptive: when several consecutive full scans find nothing new, the full-scan
 * interval backs off (x2, up to 4x); it snaps back as soon as something changes.
 */
class DeviceDiscoveryService : public QObject
{
    Q_OBJECT
public:
    static DeviceDiscoveryService &instance();

    void start();
    void stop();
    bool isRunning() const { return m_running; }

    void quickRefresh();                       // A + B
    void fullScan();                           // C + D + E (+ connect)
    void cancelScan();
    void connectEndpoint(const QString &endpoint);           // manual "adb connect host:port"
    void connectRange(const QString &rangeText, quint16 port);    // manual sweep
    void disconnectEndpoint(const QString &endpoint);

    bool isScanning() const { return m_scanning; }
    int scanDone() const { return m_scanDone; }
    int scanTotal() const { return m_scanTotal; }
    QDateTime lastFullScan() const { return m_lastFullScan; }
    qint64 lastScanDurationMs() const { return m_lastScanMs; }
    int lastScanFound() const { return m_lastScanFound; }
    QList<adb::AdbDeviceInfo> lastSnapshot() const { return m_lastSnapshot; }
    int pendingConnects() const { return static_cast<int>(m_connectQueue.size() + m_connecting.size()); }
    bool mdnsSupported() const { return m_mdnsSupported; }

signals:
    void scanStarted(int total);
    void scanProgress(int done, int total);
    void scanFinished(int hostsFound, int newlyConnected, qint64 elapsedMs);
    void snapshotUpdated(const QList<farm::adb::AdbDeviceInfo> &devices);
    void deviceAppeared(const QString &id);        // now "device" in adb
    void deviceDisappeared(const QString &id);     // was online, no longer listed
    void statusMessage(const QString &text);

private:
    explicit DeviceDiscoveryService(QObject *parent = nullptr);
    void applySettings();
    void onScannerHost(const QString &host);
    void onScannerFinished(const QStringList &found, qint64 ms, bool cancelled);
    void enqueueConnect(const QString &endpoint, bool manual);
    void pumpConnects();
    void readArpNeighbours();
    QStringList localAddresses() const;
    void onQuickRefreshResult(const QList<adb::AdbDeviceInfo> &devices);

    QTimer m_quickTimer;
    QTimer m_fullTimer;
    NetworkScanner *m_scanner = nullptr;
    CancellationToken m_scanToken;
    bool m_running = false;
    bool m_scanning = false;
    bool m_quickInFlight = false;
    bool m_mdnsSupported = true;
    int m_scanDone = 0;
    int m_scanTotal = 0;
    int m_scanNewlyConnected = 0;
    int m_lastScanFound = 0;
    int m_idleScans = 0;
    qint64 m_lastScanMs = 0;
    QDateTime m_lastFullScan;
    QStringList m_arpHosts;
    QSet<QString> m_onlineIds;                  // ids that were "device" at the last snapshot
    QList<adb::AdbDeviceInfo> m_lastSnapshot;
    QStringList m_connectQueue;
    QSet<QString> m_connecting;
    QSet<QString> m_manualConnects;
    QHash<QString, QDateTime> m_recentConnectAttempt;    // avoid hammering unauthorized hosts
    int m_connectConcurrency = 8;
    int m_connectTimeoutMs = 6000;
};

} // namespace farm

#endif // FARM_DISCOVERY_DEVICEDISCOVERYSERVICE_H
