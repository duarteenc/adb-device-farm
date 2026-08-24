#ifndef FARM_DEVICES_KEEPAWAKEMANAGER_H
#define FARM_DEVICES_KEEPAWAKEMANAGER_H

#include <QDateTime>
#include <QHash>
#include <QObject>
#include <QSet>
#include <QStringList>
#include <QTimer>

#include "devicerecord.h"

namespace farm {

/**
 * Keeps farm devices from sleeping.
 *
 * Policy (per device > per group > global Settings): when ON, apply
 *   svc power stayon true
 *   settings put global stay_on_while_plugged_in 7
 *   settings put system screen_off_timeout 2147483647
 * then READ the settings back and only report "Active" when they stuck. Vendor
 * ROMs that reject a value are reported as "Failed: <diagnostic>" — never assumed.
 *
 * A periodic health check (Settings › Keep Awake › check interval, round-robin so
 * 100 phones don't all get polled at once) reads the power state; a display that
 * went off is woken with KEYCODE_WAKEUP. A keyguard is reported as "Awake but
 * locked" — the lock is never bypassed.
 *
 * Policies are re-applied after reconnect, reboot, discovery and app restart.
 */
class KeepAwakeManager : public QObject
{
    Q_OBJECT
public:
    static KeepAwakeManager &instance();

    void start();
    void stop();

    bool policyFor(const QString &id) const;       // effective ON/OFF
    void applyPolicy(const QString &id);           // apply (or restore when OFF)
    void applyPolicy(const QStringList &ids);
    void applyAllOnline();
    void restoreDefaults(const QString &id);       // explicit "restore 30 s timeout"
    void wakeDevice(const QString &id);
    void checkScreen(const QString &id);
    QString status(const QString &id) const { return m_status.value(id); }
    int activeCount() const;
    int failedCount() const;

signals:
    void statusChanged(const QString &id, const QString &status);

private:
    explicit KeepAwakeManager(QObject *parent = nullptr);
    void tick();
    void setStatus(const QString &id, const QString &status);

    QTimer m_timer;
    QHash<QString, QString> m_status;
    QHash<QString, QDateTime> m_lastApplied;
    QStringList m_roundRobin;
    int m_rrIndex = 0;
    bool m_running = false;
};

} // namespace farm

#endif // FARM_DEVICES_KEEPAWAKEMANAGER_H
