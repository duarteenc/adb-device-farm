#ifndef FARM_DEVICES_DEVICESERVICE_H
#define FARM_DEVICES_DEVICESERVICE_H

#include <QHash>
#include <QObject>
#include <QPointer>
#include <QQueue>
#include <QSet>
#include <QSize>
#include <QStringList>
#include <QTimer>

#include "QtScrcpyCore.h"
#include "devicerecord.h"

namespace farm {

struct MirrorProfile
{
    int maxSize = 800;
    int bitRate = 4000000;
    int maxFps = 30;
    bool stayAwake = true;
    QString name;    // preset name for display
};

/**
 * Owns the per-device connection/mirror lifecycle on top of qsc::IDeviceManage:
 *
 *  - bounded "mirror start" queue (server push + first frame is the expensive part)
 *  - per-start timeout so a hung phone never blocks the queue
 *  - unique scid/port lease per session (ConnectionIdAllocator), released on stop
 *  - optional resolution/density normalisation before capture (async, timed)
 *  - automatic reconnect with exponential backoff (1,2,5,10,30,60 s) for devices
 *    that drop while they were wanted; operator "Stop" disables it for that device
 *  - quality presets per device / group / global, with adaptive downgrade when
 *    many mirrors are active (only if Adaptive Quality is enabled)
 *
 * GUI thread only (the QtScrcpy core is not thread-safe). All blocking work goes
 * through AdbExecutor.
 */
class DeviceService : public QObject
{
    Q_OBJECT
public:
    static DeviceService &instance();

    void start();
    void shutdown();

    // ---- mirror control ----
    void startMirror(const QString &id);
    void startMirror(const QStringList &ids);
    void startMirrorAll();
    void stopMirror(const QString &id, bool byOperator = true);
    void stopMirror(const QStringList &ids);
    void stopMirrorAll();
    void restartMirror(const QString &id);
    bool isMirroring(const QString &id) const;
    bool isStarting(const QString &id) const { return m_starting.contains(id) || m_queue.contains(id); }
    QStringList mirroringIds() const { return m_mirroring.values(); }
    int mirroringCount() const { return static_cast<int>(m_mirroring.size()); }
    int queuedCount() const { return static_cast<int>(m_queue.size()); }
    int startingCount() const { return static_cast<int>(m_starting.size()); }
    QPointer<qsc::IDevice> device(const QString &id) const;
    QSize frameSize(const QString &id) const { return m_frameSizes.value(id); }

    // ---- connection control (adb level) ----
    void connectDevice(const QString &id);       // adb connect for TCP, then mirror if wanted
    void disconnectDevice(const QString &id);    // stop mirror + adb disconnect, no auto reconnect
    void reconnectDevice(const QString &id);     // force an immediate reconnect attempt
    void rebootDevice(const QString &id);        // reboot and reconnect when it comes back

    // ---- profiles ----
    MirrorProfile globalProfile() const;
    MirrorProfile profileFor(const QString &id) const;
    void setGlobalProfile(const MirrorProfile &profile, bool applyToRunning);
    static MirrorProfile presetProfile(const QString &name);
    QString serverPath() const;

    // ---- stats ----
    int reconnectCount() const { return m_reconnectTotal; }
    int disconnectCount() const { return m_disconnectTotal; }
    qint64 lastConnectLatencyMs() const { return m_lastConnectLatencyMs; }
    double averageConnectLatencyMs() const { return m_connectSamples ? static_cast<double>(m_connectLatencyTotalMs) / m_connectSamples : 0.0; }

signals:
    void mirrorStarted(const QString &id, const QSize &frameSize);
    void mirrorStopped(const QString &id);
    void mirrorFailed(const QString &id, const QString &reason);
    void reconnectScheduled(const QString &id, int attempt, int delayMs);
    void queueChanged();

private:
    explicit DeviceService(QObject *parent = nullptr);
    void pump();
    void beginStart(const QString &id);
    void launchCore(const QString &id);
    void onCoreConnected(bool success, const QString &serial, const QString &deviceName, const QSize &size);
    void onCoreDisconnected(const QString &serial);
    void finishStart(const QString &id, bool success, const QString &reason);
    void scheduleReconnect(const QString &id);
    void attemptReconnect(const QString &id);
    void cancelReconnect(const QString &id);
    void onDeviceAppeared(const QString &id);
    void onDeviceDisappeared(const QString &id);
    void applyAdaptiveQuality();
    int maxConcurrentStarts() const;

    QQueue<QString> m_queue;                  // waiting for a start slot
    QSet<QString> m_starting;                 // server push / first frame in progress
    QSet<QString> m_mirroring;                // live sessions
    QSet<QString> m_wanted;                   // should be mirroring (drives reconnect)
    QSet<QString> m_operatorStopped;          // explicit Stop -> no auto reconnect/mirror
    QSet<QString> m_normalized;               // wm size applied this session
    QSet<QString> m_rebooting;
    QHash<QString, QTimer *> m_startTimers;
    QHash<QString, QTimer *> m_reconnectTimers;
    QHash<QString, int> m_reconnectAttempts;
    QHash<QString, qint64> m_startedAtMs;
    QHash<QString, QSize> m_frameSizes;
    QHash<QString, MirrorProfile> m_activeProfiles;
    MirrorProfile m_global;
    int m_reconnectTotal = 0;
    int m_disconnectTotal = 0;
    int m_connectSamples = 0;
    qint64 m_connectLatencyTotalMs = 0;
    qint64 m_lastConnectLatencyMs = 0;
    bool m_started = false;
    bool m_shuttingDown = false;
};

} // namespace farm

#endif // FARM_DEVICES_DEVICESERVICE_H
