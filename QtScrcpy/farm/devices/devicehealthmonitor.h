#ifndef FARM_DEVICES_DEVICEHEALTHMONITOR_H
#define FARM_DEVICES_DEVICEHEALTHMONITOR_H

#include <QDateTime>
#include <QHash>
#include <QObject>
#include <QSet>
#include <QStringList>
#include <QTimer>

namespace farm {

/**
 * Periodic, staggered health collection for online devices: battery / charging /
 * temperature, free storage, uptime, WiFi RSSI, ADB round-trip latency and the
 * device identity properties (model, manufacturer, Android version, SDK, hardware
 * serial) the first time a device is seen. One combined `adb shell` per device per
 * interval — never a `dumpsys` storm every second.
 *
 * Threshold crossings (battery below / above X, temperature above X) are emitted
 * once per crossing so the scheduler and notifications can react.
 */
class DeviceHealthMonitor : public QObject
{
    Q_OBJECT
public:
    static DeviceHealthMonitor &instance();

    void start();
    void stop();
    void refresh(const QString &id);          // immediate one-off
    void refreshAll();
    void collectIdentity(const QString &id);  // getprop snapshot

signals:
    void healthUpdated(const QString &id);
    void batteryBelow(const QString &id, int level);
    void batteryAbove(const QString &id, int level);
    void temperatureAbove(const QString &id, double celsius);

private:
    explicit DeviceHealthMonitor(QObject *parent = nullptr);
    void tick();
    void applyResult(const QString &id, const QString &stdOut, qint64 roundTripMs);

    QTimer m_timer;
    QStringList m_roundRobin;
    int m_rrIndex = 0;
    QSet<QString> m_inFlight;
    QSet<QString> m_identityDone;
    QHash<QString, bool> m_lowBatteryFlag;
    QHash<QString, bool> m_hotFlag;
    bool m_running = false;
};

} // namespace farm

#endif // FARM_DEVICES_DEVICEHEALTHMONITOR_H
