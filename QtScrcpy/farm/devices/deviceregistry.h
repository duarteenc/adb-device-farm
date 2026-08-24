#ifndef FARM_DEVICES_DEVICEREGISTRY_H
#define FARM_DEVICES_DEVICEREGISTRY_H

#include <functional>

#include <QHash>
#include <QList>
#include <QMutex>
#include <QObject>
#include <QSet>
#include <QStringList>
#include <QTimer>

#include "../adb/adbparsers.h"
#include "../storage/repositories.h"
#include "devicerecord.h"

namespace farm {

/**
 * The single source of truth for "which devices exist and what do we know about
 * them". Lives on the GUI thread; every mutation goes through update()/setState()
 * so the UI receives one signal per change. Persisted through DeviceRepository
 * (debounced), so metadata survives restarts and devices reappear as "Offline"
 * until discovery sees them again.
 */
class DeviceRegistry : public QObject
{
    Q_OBJECT
public:
    enum class SortKey { Number, Name, Ip, Model, Battery, Group, Online, Automation, Latency, LastSeen };

    static DeviceRegistry &instance();

    void load();          // from the database (+ one-time import of legacy QSettings groups)
    void flush();         // persist dirty records immediately

    bool contains(const QString &id) const { return m_devices.contains(id); }
    DeviceRecord get(const QString &id) const { return m_devices.value(id); }
    /// Copy of a record that is safe to read from worker threads (automation lanes).
    DeviceRecord snapshot(const QString &id) const;
    QList<DeviceRecord> all() const { return m_devices.values(); }
    QStringList ids() const { return m_devices.keys(); }
    int count() const { return static_cast<int>(m_devices.size()); }
    int countInState(DeviceState state) const;
    QStringList idsInState(DeviceState state) const;
    QStringList onlineIds() const;

    /// Apply `fn` to the record, persist, emit deviceChanged.
    bool update(const QString &id, const std::function<void(DeviceRecord &)> &fn);
    /// Same but for runtime-only fields (health, fps): emits, does not persist.
    bool updateRuntime(const QString &id, const std::function<void(DeviceRecord &)> &fn);

    /// Merge an `adb devices -l` entry; creates the record on first sight. Returns the id.
    QString upsertFromAdb(const adb::AdbDeviceInfo &info);
    /// A TCP host answered on the ADB port but is not (yet) connected.
    QString markDiscovered(const QString &host, int port);
    void setState(const QString &id, DeviceState state, const QString &message = QString());
    void remove(const QString &id);
    void forget(const QString &id) { remove(id); }

    // ---- groups ----
    QList<GroupInfo> groups() const { return m_groups; }
    GroupInfo group(const QString &name) const;
    bool hasGroup(const QString &name) const;
    bool createGroup(const QString &name, const QString &color = QString());
    bool renameGroup(const QString &oldName, const QString &newName);
    bool deleteGroup(const QString &name);
    bool setGroupColor(const QString &name, const QString &color);
    bool setGroupSettings(const QString &name, const QVariantMap &settings);
    bool setGroupOrder(const QStringList &orderedNames);
    QStringList membersOf(const QString &groupName) const;
    void assignGroup(const QStringList &ids, const QString &groupName);    // empty = ungroup

    // ---- numbering ----
    void autoNumber();                                   // 1..N by numeric IP / serial order, keeps manual numbers
    void renumber(const QStringList &ids, int startAt);  // sequential over the given order
    void setNumber(const QString &id, int number);
    int nextFreeNumber() const;

    // ---- sorting / filtering ----
    QStringList sorted(SortKey key, bool ascending = true, const QStringList &subset = QStringList()) const;
    QStringList search(const QString &query, const QStringList &subset = QStringList()) const;
    QStringList favorites() const;
    QStringList recentlyOffline(int minutes = 30) const;
    QStringList recentlyConnected(int minutes = 30) const;
    QStringList automationRunning() const;
    QStringList byModel(const QString &model) const;
    QStringList byAndroidVersion(const QString &version) const;

signals:
    void loaded();
    void deviceAdded(const QString &id);
    void deviceChanged(const QString &id);
    void deviceRemoved(const QString &id);
    void stateChanged(const QString &id, farm::DeviceState oldState, farm::DeviceState newState);
    void groupsChanged();

private:
    explicit DeviceRegistry(QObject *parent = nullptr);
    void markDirty(const QString &id);
    void syncSnapshot(const QString &id);
    void syncAllSnapshots();
    void importLegacyGroups();
    static QString colorForIndex(int index);

    QHash<QString, DeviceRecord> m_devices;
    mutable QMutex m_snapshotMutex;
    QHash<QString, DeviceRecord> m_snapshot;    // mirror of m_devices readable off the GUI thread
    QList<GroupInfo> m_groups;
    QSet<QString> m_dirty;
    QTimer m_flushTimer;
    bool m_loaded = false;
};

} // namespace farm

#endif // FARM_DEVICES_DEVICEREGISTRY_H
