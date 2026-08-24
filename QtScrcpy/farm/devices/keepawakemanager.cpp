#include "keepawakemanager.h"

#include <algorithm>

#include "../adb/adbexecutor.h"
#include "../adb/adbparsers.h"
#include "../core/activitylog.h"
#include "../core/farmlog.h"
#include "../core/farmsettings.h"
#include "deviceregistry.h"

namespace farm {

namespace {
const char *kComponent = "keepawake";

const char *kApplyScript =
    "svc power stayon true; "
    "settings put global stay_on_while_plugged_in 7; "
    "settings put system screen_off_timeout 2147483647; "
    "echo SOWP=$(settings get global stay_on_while_plugged_in); "
    "echo SOT=$(settings get system screen_off_timeout)";

const char *kRestoreScript =
    "svc power stayon false; "
    "settings put global stay_on_while_plugged_in 0; "
    "settings put system screen_off_timeout 30000; "
    "echo SOWP=$(settings get global stay_on_while_plugged_in); "
    "echo SOT=$(settings get system screen_off_timeout)";

const char *kScreenProbe =
    "dumpsys power | grep -E 'mWakefulness=|Display Power' ; "
    "dumpsys window policy 2>/dev/null | grep -E 'mShowingLockscreen|isKeyguardShowing|mKeyguardShowing|showing=' | head -3";
} // namespace

KeepAwakeManager &KeepAwakeManager::instance()
{
    static KeepAwakeManager manager;
    return manager;
}

KeepAwakeManager::KeepAwakeManager(QObject *parent)
    : QObject(parent)
{
    m_timer.setSingleShot(false);
    connect(&m_timer, &QTimer::timeout, this, &KeepAwakeManager::tick);
}

void KeepAwakeManager::start()
{
    if (m_running) {
        return;
    }
    m_running = true;
    DeviceRegistry &registry = DeviceRegistry::instance();
    // Re-apply whenever a device (re)enters an online state.
    connect(&registry, &DeviceRegistry::stateChanged, this, [this](const QString &id, DeviceState oldState, DeviceState newState) {
        if (!deviceStateIsOnline(oldState) && deviceStateIsOnline(newState) && FarmSettings::instance().keepAwakeReapplyOnReconnect()) {
            applyPolicy(id);
        }
    });
    connect(&registry, &DeviceRegistry::deviceRemoved, this, [this](const QString &id) {
        m_status.remove(id);
        m_lastApplied.remove(id);
    });
    connect(&FarmSettings::instance(), &FarmSettings::changed, this, [this](const QString &key) {
        if (key == QLatin1String("keepawake/enabled")) {
            applyAllOnline();
        } else if (key == QLatin1String("keepawake/checkSeconds")) {
            m_timer.start(std::clamp(FarmSettings::instance().wakeCheckSeconds(), 10, 3600) * 1000 / 4);
        }
    });
    // The tick fires 4x per interval and handles a quarter of the fleet each time.
    m_timer.start(std::clamp(FarmSettings::instance().wakeCheckSeconds(), 10, 3600) * 1000 / 4);
}

void KeepAwakeManager::stop()
{
    m_running = false;
    m_timer.stop();
}

bool KeepAwakeManager::policyFor(const QString &id) const
{
    const DeviceRegistry &registry = DeviceRegistry::instance();
    const DeviceRecord r = registry.get(id);
    if (r.keepAwake == 0) {
        return false;
    }
    if (r.keepAwake == 1) {
        return true;
    }
    if (!r.group.isEmpty()) {
        const QVariant g = registry.group(r.group).settings.value(QStringLiteral("keepAwake"), -1);
        if (g.toInt() == 0) {
            return false;
        }
        if (g.toInt() == 1) {
            return true;
        }
    }
    return FarmSettings::instance().keepAwake();
}

void KeepAwakeManager::setStatus(const QString &id, const QString &status)
{
    if (m_status.value(id) == status) {
        return;
    }
    m_status.insert(id, status);
    DeviceRegistry::instance().updateRuntime(id, [&status](DeviceRecord &r) { r.keepAwakeStatus = status; });
    emit statusChanged(id, status);
}

void KeepAwakeManager::applyPolicy(const QString &id)
{
    const DeviceRecord r = DeviceRegistry::instance().get(id);
    if (!r.isOnline()) {
        return;
    }
    const bool on = policyFor(id);
    m_lastApplied.insert(id, QDateTime::currentDateTime());
    AdbExecutor::instance().shell(id, QLatin1String(on ? kApplyScript : kRestoreScript), this, [this, id, on](const AdbResult &res) {
        if (!res.ok) {
            setStatus(id, tr("Failed: %1").arg(res.error));
            FarmLog::instance().warning(QLatin1String(kComponent), QStringLiteral("apply failed: %1").arg(res.error), id);
            return;
        }
        // Verify the values actually stuck (vendor ROMs may silently ignore them).
        const QString out = res.stdOut;
        bool sowpOk = false;
        bool sotOk = false;
        for (const QString &line : out.split(QLatin1Char('\n'))) {
            const QString t = line.trimmed();
            if (t.startsWith(QLatin1String("SOWP="))) {
                sowpOk = on ? (t.mid(5).toInt() & 7) == 7 : t.mid(5).toInt() == 0;
            } else if (t.startsWith(QLatin1String("SOT="))) {
                sotOk = on ? t.mid(4).toLongLong() >= 2147483647LL : t.mid(4).toLongLong() == 30000;
            }
        }
        if (!on) {
            setStatus(id, tr("Off"));
            ActivityLog::instance().info(ActivityEntry::Power, tr("Keep-awake restored to defaults on %1").arg(id), id);
            return;
        }
        if (sowpOk && sotOk) {
            setStatus(id, tr("Active"));
            ActivityLog::instance().info(ActivityEntry::Power, tr("Keep-awake applied to %1").arg(DeviceRegistry::instance().get(id).displayName()), id);
        } else {
            const QString diag = QStringLiteral("stay_on_while_plugged_in %1, screen_off_timeout %2 (%3)")
                                     .arg(sowpOk ? QStringLiteral("ok") : QStringLiteral("rejected"), sotOk ? QStringLiteral("ok") : QStringLiteral("rejected"), out.simplified());
            setStatus(id, tr("Failed: %1").arg(diag));
            ActivityLog::instance().warning(ActivityEntry::Power, tr("Keep-awake partially failed on %1: %2").arg(id, diag), id);
        }
    }, 10000);
}

void KeepAwakeManager::applyPolicy(const QStringList &ids)
{
    for (const QString &id : ids) {
        applyPolicy(id);
    }
}

void KeepAwakeManager::applyAllOnline()
{
    applyPolicy(DeviceRegistry::instance().onlineIds());
}

void KeepAwakeManager::restoreDefaults(const QString &id)
{
    DeviceRegistry::instance().update(id, [](DeviceRecord &r) { r.keepAwake = 0; });
    applyPolicy(id);
}

void KeepAwakeManager::wakeDevice(const QString &id)
{
    AdbExecutor::instance().shell(id, QStringLiteral("input keyevent KEYCODE_WAKEUP"), this, [this, id](const AdbResult &res) {
        if (res.ok) {
            ActivityLog::instance().info(ActivityEntry::Power, tr("Woke display on %1").arg(id), id);
            DeviceRegistry::instance().updateRuntime(id, [](DeviceRecord &r) { r.screenOn = true; });
        } else {
            FarmLog::instance().warning(QLatin1String(kComponent), QStringLiteral("wake failed: %1").arg(res.error), id);
        }
    }, 6000);
}

void KeepAwakeManager::checkScreen(const QString &id)
{
    AdbExecutor::instance().shell(id, QLatin1String(kScreenProbe), this, [this, id](const AdbResult &res) {
        if (!res.ok) {
            return;
        }
        const adb::ScreenState s = adb::parseScreenState(res.stdOut);
        if (!s.known) {
            return;
        }
        DeviceRegistry::instance().updateRuntime(id, [&s](DeviceRecord &r) {
            r.screenOn = s.displayOn;
            r.locked = s.locked;
        });
        const bool wantAwake = policyFor(id);
        if (wantAwake && !s.displayOn) {
            if (FarmSettings::instance().wakeSleepingDevices()) {
                ActivityLog::instance().warning(ActivityEntry::Power, tr("%1 display went off — waking").arg(id), id);
                wakeDevice(id);
                // A display that keeps turning off means the settings didn't stick: re-apply.
                applyPolicy(id);
            } else {
                setStatus(id, tr("Display off"));
            }
        } else if (wantAwake && s.locked) {
            setStatus(id, tr("Awake but locked"));
        } else if (wantAwake && m_status.value(id).startsWith(QLatin1String("Awake but")) ) {
            setStatus(id, tr("Active"));
        }
    }, 8000);
}

void KeepAwakeManager::tick()
{
    // Round-robin a quarter of the online fleet per tick.
    if (m_rrIndex <= 0 || m_rrIndex >= m_roundRobin.size()) {
        m_roundRobin = DeviceRegistry::instance().onlineIds();
        m_rrIndex = 0;
    }
    if (m_roundRobin.isEmpty()) {
        return;
    }
    const int batch = std::max(1, static_cast<int>((m_roundRobin.size() + 3) / 4));
    for (int i = 0; i < batch && m_rrIndex < m_roundRobin.size(); ++i, ++m_rrIndex) {
        const QString id = m_roundRobin.at(m_rrIndex);
        if (!policyFor(id)) {
            continue;
        }
        checkScreen(id);
    }
}

int KeepAwakeManager::activeCount() const
{
    int n = 0;
    for (const QString &s : m_status) {
        if (s == QLatin1String("Active")) {
            ++n;
        }
    }
    return n;
}

int KeepAwakeManager::failedCount() const
{
    int n = 0;
    for (const QString &s : m_status) {
        if (s.startsWith(QLatin1String("Failed"))) {
            ++n;
        }
    }
    return n;
}

} // namespace farm
