#ifndef FARM_CORE_APPCONTEXT_H
#define FARM_CORE_APPCONTEXT_H

#include <QObject>
#include <QString>
#include <QStringList>

namespace farm {

/**
 * Wires the services together in the documented startup order:
 *
 *   1. load configuration            (FarmSettings)
 *   2. open local database           (Database + migrations)
 *   3. initialise ADB                 (AdbExecutor thread, adb path)
 *   4. load known devices            (DeviceRegistry)
 *   5. render UI immediately         (caller shows the window, then calls startServices)
 *   6. start USB/ADB discovery       (DeviceDiscoveryService quick refresh)
 *   7. start LAN discovery           (subnet scan)
 *   8. auto-connect known devices
 *   9. apply keep-awake policy       (KeepAwakeManager)
 *  10. start mirror sessions         (DeviceService, per settings)
 *  11. resume scheduler              (Scheduler)
 *  12. begin health monitoring       (DeviceHealthMonitor)
 *
 * Nothing here blocks: every step hands work to the executor lanes.
 */
class AppContext : public QObject
{
    Q_OBJECT
public:
    struct Options
    {
        bool farm = false;
        bool listDevices = false;
        bool scan = false;
        bool help = false;
        QString runWorkflow;
        QStringList workflowTargets;    // ids or "group:Name"
        int mockDevices = 0;
        QString dataDir;
        bool noAutoMirror = false;
    };

    static AppContext &instance();
    static Options parseArguments(const QStringList &args);
    static QString usageText();

    bool initialize(const Options &options);
    void startServices();
    void shutdown();

    const Options &options() const { return m_options; }
    bool isMock() const { return m_options.mockDevices > 0; }
    bool crashedLastTime() const { return m_crashed; }
    QString dataDirectory() const;
    QString logDirectory() const;
    QString version() const;
    bool initialized() const { return m_initialized; }
    bool servicesStarted() const { return m_servicesStarted; }

signals:
    void servicesReady();

private:
    explicit AppContext(QObject *parent = nullptr);
    Options m_options;
    bool m_initialized = false;
    bool m_servicesStarted = false;
    bool m_crashed = false;
};

} // namespace farm

#endif // FARM_CORE_APPCONTEXT_H
