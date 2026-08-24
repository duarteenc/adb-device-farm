#include "appcontext.h"

#include <QCoreApplication>
#include <QDir>
#include <QSysInfo>

#include "../adb/adbexecutor.h"
#include "../automation/workflowengine.h"
#include "../mock/mockdeviceprovider.h"
#include "../performance/perfmonitor.h"
#include "../scheduler/scheduler.h"
#include "../devices/devicehealthmonitor.h"
#include "../devices/deviceregistry.h"
#include "../devices/deviceservice.h"
#include "../devices/keepawakemanager.h"
#include "../discovery/devicediscoveryservice.h"
#include "../storage/database.h"
#include "../storage/repositories.h"
#include "activitylog.h"
#include "farmlog.h"
#include "farmsettings.h"
#include "taskexecutor.h"

namespace farm {

AppContext &AppContext::instance()
{
    static AppContext context;
    return context;
}

AppContext::AppContext(QObject *parent)
    : QObject(parent)
{
}

AppContext::Options AppContext::parseArguments(const QStringList &args)
{
    Options o;
    for (int i = 1; i < args.size(); ++i) {
        const QString a = args.at(i);
        auto next = [&]() { return (i + 1 < args.size()) ? args.at(++i) : QString(); };
        if (a == QLatin1String("--farm")) {
            o.farm = true;
        } else if (a == QLatin1String("--list-devices")) {
            o.listDevices = true;
        } else if (a == QLatin1String("--scan")) {
            o.scan = true;
        } else if (a == QLatin1String("--run-workflow")) {
            o.runWorkflow = next();
            o.farm = true;
        } else if (a == QLatin1String("--targets")) {
            o.workflowTargets = next().split(QLatin1Char(','), Qt::SkipEmptyParts);
        } else if (a == QLatin1String("--mock-devices")) {
            o.mockDevices = next().toInt();
            o.farm = true;
        } else if (a == QLatin1String("--data-dir")) {
            o.dataDir = next();
        } else if (a == QLatin1String("--no-auto-mirror")) {
            o.noAutoMirror = true;
        } else if (a == QLatin1String("--help") || a == QLatin1String("-h") || a == QLatin1String("/?")) {
            o.help = true;
        }
    }
    return o;
}

QString AppContext::usageText()
{
    return QStringLiteral(
        "ADB Device Farm\n"
        "  QtScrcpy.exe --farm                      open the device-farm control center\n"
        "  QtScrcpy.exe --list-devices              print known/online devices and exit\n"
        "  QtScrcpy.exe --scan                      scan the configured subnet for ADB devices and exit\n"
        "  QtScrcpy.exe --run-workflow \"Name\" [--targets id1,id2,group:Box1]\n"
        "                                           run a saved workflow (headless UI) \n"
        "  QtScrcpy.exe --farm --mock-devices N     start with N simulated devices (UI/automation testing)\n"
        "  QtScrcpy.exe --data-dir PATH             override the settings/database directory\n"
        "  QtScrcpy.exe --no-auto-mirror            do not auto-start mirrors on this launch\n"
        "  (no arguments)                           the original single-device QtScrcpy UI\n");
}

QString AppContext::dataDirectory() const
{
    return FarmSettings::instance().dataDirectory();
}

QString AppContext::logDirectory() const
{
    return dataDirectory() + QStringLiteral("/logs");
}

QString AppContext::version() const
{
    return QCoreApplication::applicationVersion().isEmpty() ? QStringLiteral("3.0.0-dev") : QCoreApplication::applicationVersion();
}

bool AppContext::initialize(const Options &options)
{
    if (m_initialized) {
        return true;
    }
    m_options = options;

    // 1. configuration
    FarmSettings &settings = FarmSettings::instance();
    if (!options.dataDir.isEmpty()) {
        settings.setDataDirectory(options.dataDir);
    }
    QDir().mkpath(settings.dataDirectory());

    // logging + crash detection
    FarmLog &log = FarmLog::instance();
    // `--list-devices` / `--scan` may run beside the control center: they must
    // neither report a crash nor delete the running instance's session marker.
    log.setSessionMarkerEnabled(!options.listDevices && !options.scan);
    log.open(logDirectory());
    m_crashed = log.previousSessionCrashed();
    log.info(QStringLiteral("app"), QStringLiteral("=== ADB Device Farm %1 starting (Qt %2, %3) ===").arg(version(), QString::fromLatin1(qVersion()), QSysInfo::prettyProductName()));
    if (m_crashed) {
        log.warning(QStringLiteral("app"), QStringLiteral("previous session did not shut down cleanly"));
    }

    // 2. database
    Database &db = Database::instance();
    if (!db.open(settings.dataDirectory() + QStringLiteral("/farm.db"))) {
        log.error(QStringLiteral("app"), QStringLiteral("database unavailable: %1 — continuing without persistence").arg(db.lastError()));
    } else {
        CommandRepository::seedDefaultsIfEmpty();
    }

    // 3. adb
    AdbExecutor &adb = AdbExecutor::instance();
    adb.setAdbPath(settings.adbPath());
    adb.setMaxConcurrency(settings.adbConcurrency());
    adb.start();
    TaskExecutor::instance().setLaneConcurrency(QStringLiteral("automation"), settings.automationConcurrency());
    connect(&settings, &FarmSettings::changed, this, [](const QString &key) {
        if (key == QLatin1String("adb/concurrency")) {
            AdbExecutor::instance().setMaxConcurrency(FarmSettings::instance().adbConcurrency());
        } else if (key == QLatin1String("adb/path")) {
            AdbExecutor::instance().setAdbPath(FarmSettings::instance().adbPath());
        } else if (key == QLatin1String("automation/concurrency")) {
            TaskExecutor::instance().setLaneConcurrency(QStringLiteral("automation"), FarmSettings::instance().automationConcurrency());
        }
    });

    // 4. known devices
    DeviceRegistry::instance().load();
    // --no-auto-mirror is honoured by DeviceService for this launch only (never persisted).

    ActivityLog::instance().info(ActivityEntry::System, QStringLiteral("ADB Device Farm %1 started").arg(version()));
    if (m_crashed) {
        ActivityLog::instance().warning(ActivityEntry::System, QStringLiteral("Previous session ended unexpectedly — configuration was preserved"));
    }
    m_initialized = true;
    return true;
}

void AppContext::startServices()
{
    if (!m_initialized || m_servicesStarted) {
        return;
    }
    m_servicesStarted = true;
    PerfMonitor::instance().start();
    if (isMock()) {
        // The mock layer feeds the registry directly; no ADB/discovery.
        FarmLog::instance().info(QStringLiteral("app"), QStringLiteral("mock mode: %1 simulated devices").arg(m_options.mockDevices));
        MockDeviceProvider::instance().start(m_options.mockDevices, 10);
        if (FarmSettings::instance().autoMirror() && !m_options.noAutoMirror) {
            MockDeviceProvider::instance().streamAll(true);    // mock devices follow Auto Mirror like real ones
        }
        Scheduler::instance().start();
        emit servicesReady();
        return;
    }
    // 10. mirror sessions follow discovery events (DeviceService listens to it)
    DeviceService::instance().start();
    // 9. keep-awake reacts to devices coming online
    KeepAwakeManager::instance().start();
    // 12. health
    DeviceHealthMonitor::instance().start();
    // 6/7/8. discovery + auto-connect
    DeviceDiscoveryService::instance().start();
    // 11. scheduler
    Scheduler::instance().start();
    emit servicesReady();
}

void AppContext::shutdown()
{
    if (!m_initialized) {
        return;
    }
    FarmLog::instance().info(QStringLiteral("app"), QStringLiteral("shutting down"));
    Scheduler::instance().stop();
    PerfMonitor::instance().stop();
    MockDeviceProvider::instance().stop();
    DeviceDiscoveryService::instance().stop();
    DeviceHealthMonitor::instance().stop();
    KeepAwakeManager::instance().stop();
    DeviceService::instance().shutdown();
    DeviceRegistry::instance().flush();
    WorkflowEngine::instance().shutdown();    // cancel workers before the executors stop
    AdbExecutor::instance().stop();
    TaskExecutor::instance().shutdown(3000);
    Database::instance().close();
    FarmLog::instance().markCleanShutdown();
    FarmLog::instance().close();
    m_initialized = false;
    m_servicesStarted = false;
}

} // namespace farm
