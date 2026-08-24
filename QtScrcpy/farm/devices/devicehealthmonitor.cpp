#include "devicehealthmonitor.h"

#include <algorithm>

#include "../adb/adbexecutor.h"
#include "../adb/adbparsers.h"
#include "../core/activitylog.h"
#include "../core/farmlog.h"
#include "../core/farmsettings.h"
#include "deviceregistry.h"

namespace farm {

namespace {
const char *kHealthScript =
    "echo '#BAT'; dumpsys battery 2>/dev/null | grep -E 'level|status|powered|temperature|voltage'; "
    "echo '#DF'; df -k /data 2>/dev/null | tail -1; "
    "echo '#UP'; cat /proc/uptime 2>/dev/null; "
    "echo '#WIFI'; dumpsys wifi 2>/dev/null | grep -m1 -E 'RSSI|mWifiInfo' ; "
    "echo '#SCREEN'; dumpsys power 2>/dev/null | grep -E 'mWakefulness=|Display Power'; "
    "echo '#END'";

const char *kIdentityScript =
    "getprop ro.product.model; getprop ro.product.manufacturer; getprop ro.build.version.release; "
    "getprop ro.build.version.sdk; getprop ro.serialno; getprop ro.product.cpu.abi; getprop ro.product.brand; "
    "getprop ro.build.display.id; wm size";
} // namespace

DeviceHealthMonitor &DeviceHealthMonitor::instance()
{
    static DeviceHealthMonitor monitor;
    return monitor;
}

DeviceHealthMonitor::DeviceHealthMonitor(QObject *parent)
    : QObject(parent)
{
    connect(&m_timer, &QTimer::timeout, this, &DeviceHealthMonitor::tick);
}

void DeviceHealthMonitor::start()
{
    if (m_running) {
        return;
    }
    m_running = true;
    DeviceRegistry &registry = DeviceRegistry::instance();
    connect(&registry, &DeviceRegistry::stateChanged, this, [this](const QString &id, DeviceState oldState, DeviceState newState) {
        if (!deviceStateIsOnline(oldState) && deviceStateIsOnline(newState)) {
            if (!m_identityDone.contains(id)) {
                collectIdentity(id);
            }
            refresh(id);
        }
    });
    connect(&registry, &DeviceRegistry::deviceRemoved, this, [this](const QString &id) {
        m_inFlight.remove(id);
        m_identityDone.remove(id);
        m_lowBatteryFlag.remove(id);
        m_hotFlag.remove(id);
    });
    connect(&FarmSettings::instance(), &FarmSettings::changed, this, [this](const QString &key) {
        if (key.startsWith(QLatin1String("health/"))) {
            m_timer.start(std::clamp(FarmSettings::instance().healthIntervalSeconds(), 10, 3600) * 1000 / 6);
        }
    });
    // Six ticks per interval, each handling a sixth of the online fleet.
    m_timer.start(std::clamp(FarmSettings::instance().healthIntervalSeconds(), 10, 3600) * 1000 / 6);
}

void DeviceHealthMonitor::stop()
{
    m_running = false;
    m_timer.stop();
}

void DeviceHealthMonitor::tick()
{
    if (!FarmSettings::instance().healthMonitor()) {
        return;
    }
    if (m_rrIndex <= 0 || m_rrIndex >= m_roundRobin.size()) {
        m_roundRobin = DeviceRegistry::instance().onlineIds();
        m_rrIndex = 0;
    }
    if (m_roundRobin.isEmpty()) {
        return;
    }
    const int batch = std::max(1, static_cast<int>((m_roundRobin.size() + 5) / 6));
    for (int i = 0; i < batch && m_rrIndex < m_roundRobin.size(); ++i, ++m_rrIndex) {
        refresh(m_roundRobin.at(m_rrIndex));
    }
}

void DeviceHealthMonitor::refreshAll()
{
    const QStringList ids = DeviceRegistry::instance().onlineIds();
    for (const QString &id : ids) {
        refresh(id);
    }
}

void DeviceHealthMonitor::refresh(const QString &id)
{
    if (m_inFlight.contains(id)) {
        return;
    }
    m_inFlight.insert(id);
    AdbExecutor::instance().shell(id, QLatin1String(kHealthScript), this, [this, id](const AdbResult &res) {
        m_inFlight.remove(id);
        if (!res.ok) {
            DeviceRegistry::instance().updateRuntime(id, [](DeviceRecord &r) { r.latencyMs = -1; });
            return;
        }
        applyResult(id, res.stdOut, res.durationMs);
    }, 12000);
}

void DeviceHealthMonitor::applyResult(const QString &id, const QString &stdOut, qint64 roundTripMs)
{
    QHash<QString, QString> sections;
    QString current;
    for (const QString &line : stdOut.split(QLatin1Char('\n'))) {
        const QString t = line.trimmed();
        if (t.startsWith(QLatin1Char('#'))) {
            current = t.mid(1);
            continue;
        }
        sections[current] += t + QLatin1Char('\n');
    }
    const adb::BatteryInfo bat = adb::parseBattery(sections.value(QStringLiteral("BAT")));
    const qint64 freeKb = adb::parseDfFreeKb(sections.value(QStringLiteral("DF")));
    const QString up = sections.value(QStringLiteral("UP")).trimmed();
    const int rssi = adb::parseWifiRssi(sections.value(QStringLiteral("WIFI")));
    const adb::ScreenState screen = adb::parseScreenState(sections.value(QStringLiteral("SCREEN")));

    bool lowNow = false;
    bool hotNow = false;
    DeviceRegistry::instance().updateRuntime(id, [&](DeviceRecord &r) {
        if (bat.level >= 0) {
            r.battery = bat.level;
            r.charging = bat.charging();
        }
        if (bat.temperatureC >= 0) {
            r.temperatureC = bat.temperatureC;
        }
        if (freeKb >= 0) {
            r.storageFreeMb = freeKb / 1024;
        }
        if (!up.isEmpty()) {
            r.uptimeSeconds = static_cast<qint64>(up.section(QLatin1Char(' '), 0, 0).toDouble());
        }
        if (rssi != 0) {
            r.wifiRssi = rssi;
        }
        if (screen.known) {
            r.screenOn = screen.displayOn;
        }
        r.latencyMs = static_cast<int>(roundTripMs);
        r.lastHealthCheck = QDateTime::currentDateTime();
        lowNow = r.battery >= 0 && r.battery <= FarmSettings::instance().batteryLowThreshold();
        hotNow = r.temperatureC >= FarmSettings::instance().temperatureHighThreshold();
    });
    emit healthUpdated(id);

    // Edge-triggered threshold events.
    const bool wasLow = m_lowBatteryFlag.value(id, false);
    if (lowNow && !wasLow) {
        emit batteryBelow(id, bat.level);
        ActivityLog::instance().warning(ActivityEntry::Device, tr("Battery low on %1: %2%").arg(DeviceRegistry::instance().get(id).displayName()).arg(bat.level), id);
    } else if (!lowNow && wasLow) {
        emit batteryAbove(id, bat.level);
    }
    m_lowBatteryFlag.insert(id, lowNow);
    const bool wasHot = m_hotFlag.value(id, false);
    if (hotNow && !wasHot) {
        emit temperatureAbove(id, bat.temperatureC);
        ActivityLog::instance().warning(ActivityEntry::Device, tr("Temperature high on %1: %2 °C").arg(id).arg(bat.temperatureC, 0, 'f', 1), id);
    }
    m_hotFlag.insert(id, hotNow);
}

void DeviceHealthMonitor::collectIdentity(const QString &id)
{
    m_identityDone.insert(id);
    AdbExecutor::instance().shell(id, QLatin1String(kIdentityScript), this, [this, id](const AdbResult &res) {
        if (!res.ok) {
            m_identityDone.remove(id);
            return;
        }
        const QStringList lines = res.stdOut.split(QLatin1Char('\n'));
        auto at = [&lines](int i) { return i < lines.size() ? lines.at(i).trimmed() : QString(); };
        const QString model = at(0);
        const QString manufacturer = at(1);
        const QString release = at(2);
        const int sdk = at(3).toInt();
        const QString serial = at(4);
        const QString abi = at(5);
        const QString brand = at(6);
        const QString build = at(7);
        const QString wm = adb::parseWmSize(res.stdOut);
        DeviceRegistry::instance().update(id, [&](DeviceRecord &r) {
            if (!model.isEmpty()) {
                r.model = model;
            }
            if (!manufacturer.isEmpty()) {
                r.manufacturer = manufacturer;
            }
            if (!release.isEmpty()) {
                r.androidVersion = release;
            }
            if (sdk > 0) {
                r.sdk = sdk;
            }
            if (!serial.isEmpty() && serial != QLatin1String("unknown")) {
                r.hwSerial = serial;
            }
            r.props.insert(QStringLiteral("ro.product.model"), model);
            r.props.insert(QStringLiteral("ro.product.manufacturer"), manufacturer);
            r.props.insert(QStringLiteral("ro.build.version.release"), release);
            r.props.insert(QStringLiteral("ro.build.version.sdk"), sdk);
            r.props.insert(QStringLiteral("ro.serialno"), serial);
            r.props.insert(QStringLiteral("ro.product.cpu.abi"), abi);
            r.props.insert(QStringLiteral("ro.product.brand"), brand);
            r.props.insert(QStringLiteral("ro.build.display.id"), build);
            if (!wm.isEmpty()) {
                r.props.insert(QStringLiteral("wm.size"), wm);
                const QStringList parts = wm.split(QLatin1Char('x'));
                if (parts.size() == 2) {
                    r.screenSize = QSize(parts.at(0).toInt(), parts.at(1).toInt());
                }
            }
        });
        FarmLog::instance().debug(QStringLiteral("health"), QStringLiteral("identity: %1 %2 Android %3 (SDK %4) hw=%5").arg(manufacturer, model, release).arg(sdk).arg(serial), id);
    }, 10000);
}

} // namespace farm
