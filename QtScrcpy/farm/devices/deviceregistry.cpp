#include "deviceregistry.h"

#include <algorithm>

#include <QDateTime>
#include <QSettings>

#include "../core/activitylog.h"
#include "../core/farmlog.h"
#include "../core/ipv4.h"

namespace farm {

DeviceRegistry &DeviceRegistry::instance()
{
    static DeviceRegistry registry;
    return registry;
}

DeviceRegistry::DeviceRegistry(QObject *parent)
    : QObject(parent)
{
    qRegisterMetaType<farm::DeviceState>("farm::DeviceState");
    qRegisterMetaType<farm::DeviceRecord>("farm::DeviceRecord");
    m_flushTimer.setSingleShot(true);
    m_flushTimer.setInterval(750);
    connect(&m_flushTimer, &QTimer::timeout, this, &DeviceRegistry::flush);
}

void DeviceRegistry::load()
{
    m_devices.clear();
    const QList<DeviceRecord> records = DeviceRepository::loadAll();
    for (const DeviceRecord &r : records) {
        m_devices.insert(r.id, r);
    }
    m_groups = GroupRepository::loadAll();
    importLegacyGroups();
    m_loaded = true;
    FarmLog::instance().info(QStringLiteral("registry"), QStringLiteral("loaded %1 known devices, %2 groups").arg(m_devices.size()).arg(m_groups.size()));
    emit loaded();
    for (const QString &id : m_devices.keys()) {
        emit deviceAdded(id);
    }
    emit groupsChanged();
}

void DeviceRegistry::importLegacyGroups()
{
    // v2.0 stored groups in QSettings("ZamiApp","AdbDeviceFarm") / farm_groups.
    if (KvRepository::get(QStringLiteral("legacy_groups_imported")) == QLatin1String("1")) {
        return;
    }
    QSettings settings(QStringLiteral("ZamiApp"), QStringLiteral("AdbDeviceFarm"));
    settings.beginGroup(QStringLiteral("farm_groups"));
    const QStringList names = settings.value(QStringLiteral("__names__")).toStringList();
    int imported = 0;
    for (int i = 0; i < names.size(); ++i) {
        const QString name = names.at(i);
        if (name.isEmpty() || hasGroup(name)) {
            continue;
        }
        createGroup(name);
        const QStringList serials = settings.value(QStringLiteral("serials_%1").arg(i)).toStringList();
        for (const QString &serial : serials) {
            if (!m_devices.contains(serial)) {
                DeviceRecord r;
                r.id = serial;
                r.lastIp = ipv4::hostOf(serial);
                r.port = ipv4::portOf(serial);
                r.connectionType = ipv4::isTcpEndpoint(serial) ? ConnectionType::WifiAdb : ConnectionType::Usb;
                r.state = DeviceState::Offline;
                r.firstSeen = QDateTime::currentDateTime();
                m_devices.insert(serial, r);
            }
            m_devices[serial].group = name;
            m_dirty.insert(serial);
        }
        ++imported;
    }
    settings.endGroup();
    KvRepository::set(QStringLiteral("legacy_groups_imported"), QStringLiteral("1"));
    if (imported > 0) {
        flush();
        FarmLog::instance().info(QStringLiteral("registry"), QStringLiteral("imported %1 legacy groups").arg(imported));
    }
}

void DeviceRegistry::flush()
{
    if (m_dirty.isEmpty()) {
        return;
    }
    QList<DeviceRecord> records;
    for (const QString &id : m_dirty) {
        if (m_devices.contains(id)) {
            records.append(m_devices.value(id));
        }
    }
    m_dirty.clear();
    if (!records.isEmpty()) {
        DeviceRepository::saveAll(records);
    }
}

void DeviceRegistry::markDirty(const QString &id)
{
    m_dirty.insert(id);
    if (!m_flushTimer.isActive()) {
        m_flushTimer.start();
    }
}

int DeviceRegistry::countInState(DeviceState state) const
{
    int n = 0;
    for (const DeviceRecord &r : m_devices) {
        if (r.state == state) {
            ++n;
        }
    }
    return n;
}

QStringList DeviceRegistry::idsInState(DeviceState state) const
{
    QStringList list;
    for (const DeviceRecord &r : m_devices) {
        if (r.state == state) {
            list << r.id;
        }
    }
    return list;
}

QStringList DeviceRegistry::onlineIds() const
{
    QStringList list;
    for (const DeviceRecord &r : m_devices) {
        if (r.isOnline()) {
            list << r.id;
        }
    }
    return list;
}

bool DeviceRegistry::update(const QString &id, const std::function<void(DeviceRecord &)> &fn)
{
    auto it = m_devices.find(id);
    if (it == m_devices.end()) {
        return false;
    }
    fn(it.value());
    markDirty(id);
    emit deviceChanged(id);
    return true;
}

bool DeviceRegistry::updateRuntime(const QString &id, const std::function<void(DeviceRecord &)> &fn)
{
    auto it = m_devices.find(id);
    if (it == m_devices.end()) {
        return false;
    }
    fn(it.value());
    emit deviceChanged(id);
    return true;
}

QString DeviceRegistry::upsertFromAdb(const adb::AdbDeviceInfo &info)
{
    const QString id = info.serial;
    const bool isNew = !m_devices.contains(id);
    DeviceRecord &r = m_devices[id];
    if (isNew) {
        r.id = id;
        r.firstSeen = QDateTime::currentDateTime();
        r.state = DeviceState::Unknown;
        r.number = 0;
    }
    r.adbState = info.state;
    r.lastSeen = QDateTime::currentDateTime();
    if (info.isTcp) {
        r.lastIp = ipv4::hostOf(id);
        r.port = ipv4::portOf(id);
        if (r.connectionType != ConnectionType::Mdns) {
            r.connectionType = ConnectionType::WifiAdb;
        }
    } else {
        r.connectionType = ConnectionType::Usb;
    }
    if (!info.model.isEmpty()) {
        QString model = info.model;
        model.replace(QLatin1Char('_'), QLatin1Char('-'));
        if (r.model.isEmpty() || r.model.contains(QLatin1Char('-'))) {
            r.model = model;
        }
    }
    if (!info.product.isEmpty()) {
        r.props.insert(QStringLiteral("ro.product.name"), info.product);
    }
    if (!info.device.isEmpty()) {
        r.props.insert(QStringLiteral("ro.product.device"), info.device);
    }
    markDirty(id);
    if (isNew) {
        emit deviceAdded(id);
    }
    // Map raw adb state -> farm state (mirroring/busy are owned by DeviceService,
    // so only move devices that are not already in a live session).
    DeviceState target = r.state;
    if (info.state == QLatin1String("device")) {
        if (r.state != DeviceState::Mirroring && r.state != DeviceState::Busy && r.state != DeviceState::Connecting) {
            target = DeviceState::AdbOnline;
        }
    } else if (info.state == QLatin1String("unauthorized")) {
        target = DeviceState::Unauthorized;
    } else if (info.state == QLatin1String("offline")) {
        target = DeviceState::Offline;
    } else {
        target = DeviceState::Error;
        r.stateMessage = info.state;
    }
    if (target != r.state) {
        setState(id, target);
    } else {
        emit deviceChanged(id);
    }
    return id;
}

QString DeviceRegistry::markDiscovered(const QString &host, int port)
{
    const QString id = QStringLiteral("%1:%2").arg(host).arg(port);
    const bool isNew = !m_devices.contains(id);
    DeviceRecord &r = m_devices[id];
    if (isNew) {
        r.id = id;
        r.firstSeen = QDateTime::currentDateTime();
        r.lastIp = host;
        r.port = port;
        r.connectionType = ConnectionType::WifiAdb;
        r.state = DeviceState::Unknown;
        markDirty(id);
        emit deviceAdded(id);
    }
    if (r.state == DeviceState::Unknown || r.state == DeviceState::Offline) {
        setState(id, DeviceState::Discovered);
    }
    return id;
}

void DeviceRegistry::setState(const QString &id, DeviceState state, const QString &message)
{
    auto it = m_devices.find(id);
    if (it == m_devices.end()) {
        return;
    }
    DeviceRecord &r = it.value();
    const DeviceState old = r.state;
    r.stateMessage = message;
    if (old == state) {
        emit deviceChanged(id);
        return;
    }
    r.state = state;
    r.lastStateChange = QDateTime::currentDateTime();
    if (deviceStateIsOnline(state)) {
        r.lastSeen = r.lastStateChange;
        markDirty(id);
    }
    FarmLog::instance().debug(QStringLiteral("registry"), QStringLiteral("%1 -> %2 %3").arg(deviceStateName(old), deviceStateName(state), message), id);
    emit stateChanged(id, old, state);
    emit deviceChanged(id);
}

void DeviceRegistry::remove(const QString &id)
{
    if (!m_devices.contains(id)) {
        return;
    }
    m_devices.remove(id);
    m_dirty.remove(id);
    DeviceRepository::remove(id);
    emit deviceRemoved(id);
}

// ---------------------------------------------------------------- groups

GroupInfo DeviceRegistry::group(const QString &name) const
{
    for (const GroupInfo &g : m_groups) {
        if (g.name == name) {
            return g;
        }
    }
    return GroupInfo();
}

bool DeviceRegistry::hasGroup(const QString &name) const
{
    for (const GroupInfo &g : m_groups) {
        if (g.name == name) {
            return true;
        }
    }
    return false;
}

QString DeviceRegistry::colorForIndex(int index)
{
    static const char *const palette[] = { "#3b82f6", "#22c55e", "#f59e0b", "#a855f7", "#ef4444", "#06b6d4", "#ec4899", "#84cc16", "#f97316", "#14b8a6" };
    return QLatin1String(palette[index % 10]);
}

bool DeviceRegistry::createGroup(const QString &nameIn, const QString &color)
{
    const QString name = nameIn.trimmed();
    if (name.isEmpty() || hasGroup(name)) {
        return false;
    }
    GroupInfo g;
    g.name = name;
    g.color = color.isEmpty() ? colorForIndex(static_cast<int>(m_groups.size())) : color;
    g.order = static_cast<int>(m_groups.size());
    m_groups.append(g);
    GroupRepository::save(g);
    emit groupsChanged();
    return true;
}

bool DeviceRegistry::renameGroup(const QString &oldName, const QString &newNameIn)
{
    const QString newName = newNameIn.trimmed();
    if (newName.isEmpty() || !hasGroup(oldName) || hasGroup(newName)) {
        return false;
    }
    for (GroupInfo &g : m_groups) {
        if (g.name == oldName) {
            g.name = newName;
        }
    }
    for (auto it = m_devices.begin(); it != m_devices.end(); ++it) {
        if (it.value().group == oldName) {
            it.value().group = newName;
            emit deviceChanged(it.key());
        }
    }
    GroupRepository::rename(oldName, newName);
    emit groupsChanged();
    return true;
}

bool DeviceRegistry::deleteGroup(const QString &name)
{
    if (!hasGroup(name)) {
        return false;
    }
    for (int i = 0; i < m_groups.size(); ++i) {
        if (m_groups.at(i).name == name) {
            m_groups.removeAt(i);
            break;
        }
    }
    for (auto it = m_devices.begin(); it != m_devices.end(); ++it) {
        if (it.value().group == name) {
            it.value().group.clear();
            emit deviceChanged(it.key());
        }
    }
    GroupRepository::remove(name);
    emit groupsChanged();
    return true;
}

bool DeviceRegistry::setGroupColor(const QString &name, const QString &color)
{
    for (GroupInfo &g : m_groups) {
        if (g.name == name) {
            g.color = color;
            GroupRepository::save(g);
            emit groupsChanged();
            return true;
        }
    }
    return false;
}

bool DeviceRegistry::setGroupSettings(const QString &name, const QVariantMap &settings)
{
    for (GroupInfo &g : m_groups) {
        if (g.name == name) {
            g.settings = settings;
            GroupRepository::save(g);
            emit groupsChanged();
            return true;
        }
    }
    return false;
}

bool DeviceRegistry::setGroupOrder(const QStringList &orderedNames)
{
    for (GroupInfo &g : m_groups) {
        const int idx = static_cast<int>(orderedNames.indexOf(g.name));
        g.order = idx < 0 ? static_cast<int>(orderedNames.size()) : idx;
        GroupRepository::save(g);
    }
    std::sort(m_groups.begin(), m_groups.end(), [](const GroupInfo &a, const GroupInfo &b) { return a.order < b.order; });
    emit groupsChanged();
    return true;
}

QStringList DeviceRegistry::membersOf(const QString &groupName) const
{
    QStringList list;
    for (const DeviceRecord &r : m_devices) {
        if (r.group == groupName) {
            list << r.id;
        }
    }
    return sorted(SortKey::Number, true, list);
}

void DeviceRegistry::assignGroup(const QStringList &ids, const QString &groupName)
{
    if (!groupName.isEmpty() && !hasGroup(groupName)) {
        createGroup(groupName);
    }
    for (const QString &id : ids) {
        auto it = m_devices.find(id);
        if (it == m_devices.end() || it.value().group == groupName) {
            continue;
        }
        it.value().group = groupName;
        markDirty(id);
        emit deviceChanged(id);
    }
    emit groupsChanged();
}

// ---------------------------------------------------------------- numbering

void DeviceRegistry::autoNumber()
{
    // Keep numbers the operator already assigned; give the rest the lowest free
    // numbers in numeric IP / serial order so the grid reads left-to-right.
    QSet<int> used;
    QStringList unnumbered;
    for (const DeviceRecord &r : m_devices) {
        if (r.number > 0) {
            used.insert(r.number);
        } else {
            unnumbered << r.id;
        }
    }
    std::sort(unnumbered.begin(), unnumbered.end(), ipv4::lessThan);
    int next = 1;
    for (const QString &id : unnumbered) {
        while (used.contains(next)) {
            ++next;
        }
        m_devices[id].number = next;
        used.insert(next);
        markDirty(id);
        emit deviceChanged(id);
        ++next;
    }
}

void DeviceRegistry::renumber(const QStringList &ids, int startAt)
{
    int n = std::max(1, startAt);
    for (const QString &id : ids) {
        auto it = m_devices.find(id);
        if (it == m_devices.end()) {
            continue;
        }
        it.value().number = n++;
        markDirty(id);
        emit deviceChanged(id);
    }
}

void DeviceRegistry::setNumber(const QString &id, int number)
{
    update(id, [number](DeviceRecord &r) { r.number = number; });
}

int DeviceRegistry::nextFreeNumber() const
{
    QSet<int> used;
    for (const DeviceRecord &r : m_devices) {
        used.insert(r.number);
    }
    int n = 1;
    while (used.contains(n)) {
        ++n;
    }
    return n;
}

// ---------------------------------------------------------------- sorting / filtering

QStringList DeviceRegistry::sorted(SortKey key, bool ascending, const QStringList &subsetIn) const
{
    QStringList list = subsetIn.isEmpty() ? m_devices.keys() : subsetIn;
    auto rec = [this](const QString &id) { return m_devices.value(id); };
    auto tie = [](const DeviceRecord &a, const DeviceRecord &b) {
        if (a.number != b.number) {
            if (a.number == 0) {
                return false;
            }
            if (b.number == 0) {
                return true;
            }
            return a.number < b.number;
        }
        return ipv4::lessThan(a.id, b.id);
    };
    std::stable_sort(list.begin(), list.end(), [&](const QString &ia, const QString &ib) {
        const DeviceRecord a = rec(ia);
        const DeviceRecord b = rec(ib);
        bool less = false;
        bool decided = true;
        switch (key) {
        case SortKey::Number:
            less = tie(a, b);
            break;
        case SortKey::Name: {
            const int c = QString::compare(a.displayName(), b.displayName(), Qt::CaseInsensitive);
            if (c == 0) {
                decided = false;
            } else {
                less = c < 0;
            }
            break;
        }
        case SortKey::Ip:
            less = ipv4::lessThan(a.id, b.id);
            break;
        case SortKey::Model: {
            const int c = QString::compare(a.model, b.model, Qt::CaseInsensitive);
            if (c == 0) {
                decided = false;
            } else {
                less = c < 0;
            }
            break;
        }
        case SortKey::Battery:
            if (a.battery == b.battery) {
                decided = false;
            } else {
                less = a.battery < b.battery;
            }
            break;
        case SortKey::Group: {
            const int c = QString::compare(a.group, b.group, Qt::CaseInsensitive);
            if (c == 0) {
                decided = false;
            } else {
                less = c < 0;
            }
            break;
        }
        case SortKey::Online:
            if (a.isOnline() == b.isOnline()) {
                decided = false;
            } else {
                less = a.isOnline();
            }
            break;
        case SortKey::Automation:
            if (a.automationRunning == b.automationRunning) {
                decided = false;
            } else {
                less = a.automationRunning;
            }
            break;
        case SortKey::Latency:
            if (a.latencyMs == b.latencyMs) {
                decided = false;
            } else {
                less = (a.latencyMs < 0 ? 1 << 30 : a.latencyMs) < (b.latencyMs < 0 ? 1 << 30 : b.latencyMs);
            }
            break;
        case SortKey::LastSeen:
            if (a.lastSeen == b.lastSeen) {
                decided = false;
            } else {
                less = a.lastSeen > b.lastSeen;
            }
            break;
        }
        if (!decided) {
            return tie(a, b);
        }
        return ascending ? less : !less;
    });
    return list;
}

QStringList DeviceRegistry::search(const QString &queryIn, const QStringList &subset) const
{
    const QString query = queryIn.trimmed();
    QStringList source = subset.isEmpty() ? m_devices.keys() : subset;
    if (query.isEmpty()) {
        return source;
    }
    QStringList out;
    bool numeric = false;
    const int number = query.toInt(&numeric);
    for (const QString &id : source) {
        const DeviceRecord r = m_devices.value(id);
        if ((numeric && r.number == number) || r.id.contains(query, Qt::CaseInsensitive) || r.friendlyName.contains(query, Qt::CaseInsensitive)
            || r.model.contains(query, Qt::CaseInsensitive) || r.group.contains(query, Qt::CaseInsensitive)
            || r.hwSerial.contains(query, Qt::CaseInsensitive) || r.lastIp.contains(query) || r.numberString().contains(query)) {
            out << id;
        }
    }
    return out;
}

QStringList DeviceRegistry::favorites() const
{
    QStringList list;
    for (const DeviceRecord &r : m_devices) {
        if (r.favorite) {
            list << r.id;
        }
    }
    return list;
}

QStringList DeviceRegistry::recentlyOffline(int minutes) const
{
    QStringList list;
    const QDateTime cutoff = QDateTime::currentDateTime().addSecs(-60 * minutes);
    for (const DeviceRecord &r : m_devices) {
        if (!r.isOnline() && r.lastStateChange.isValid() && r.lastStateChange >= cutoff) {
            list << r.id;
        }
    }
    return list;
}

QStringList DeviceRegistry::recentlyConnected(int minutes) const
{
    QStringList list;
    const QDateTime cutoff = QDateTime::currentDateTime().addSecs(-60 * minutes);
    for (const DeviceRecord &r : m_devices) {
        if (r.isOnline() && r.lastStateChange.isValid() && r.lastStateChange >= cutoff) {
            list << r.id;
        }
    }
    return list;
}

QStringList DeviceRegistry::automationRunning() const
{
    QStringList list;
    for (const DeviceRecord &r : m_devices) {
        if (r.automationRunning) {
            list << r.id;
        }
    }
    return list;
}

QStringList DeviceRegistry::byModel(const QString &model) const
{
    QStringList list;
    for (const DeviceRecord &r : m_devices) {
        if (r.model.compare(model, Qt::CaseInsensitive) == 0) {
            list << r.id;
        }
    }
    return list;
}

QStringList DeviceRegistry::byAndroidVersion(const QString &version) const
{
    QStringList list;
    for (const DeviceRecord &r : m_devices) {
        if (r.androidVersion == version) {
            list << r.id;
        }
    }
    return list;
}

} // namespace farm
