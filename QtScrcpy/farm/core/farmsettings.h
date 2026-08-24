#ifndef FARM_CORE_FARMSETTINGS_H
#define FARM_CORE_FARMSETTINGS_H

#include <QObject>
#include <QRecursiveMutex>
#include <QSettings>
#include <QString>
#include <QVariant>

namespace farm {

/**
 * Typed, persistent application settings (INI file in the app data directory).
 *
 * Every setter emits changed(key) so services can react live (e.g. the discovery
 * timers re-arm when the intervals change). Keys are grouped by the Settings page
 * categories: general / discovery / adb / mirroring / performance / keepawake /
 * automation / scheduler / storage / notifications / advanced.
 */
class FarmSettings : public QObject
{
    Q_OBJECT
public:
    static FarmSettings &instance();

    /// Where settings/database/logs live (…/AppData/Roaming/ADBDeviceFarm by default).
    QString dataDirectory() const { return m_dataDir; }
    void setDataDirectory(const QString &dir);
    QString settingsFile() const;

    QVariant value(const QString &key, const QVariant &fallback = QVariant()) const;
    void setValue(const QString &key, const QVariant &value);
    bool boolValue(const QString &key, bool fallback) const { return value(key, fallback).toBool(); }
    int intValue(const QString &key, int fallback) const { return value(key, fallback).toInt(); }
    QString stringValue(const QString &key, const QString &fallback) const { return value(key, fallback).toString(); }

    // ---- discovery ----
    bool autoDiscovery() const { return boolValue(QStringLiteral("discovery/enabled"), true); }
    QString subnet() const { return stringValue(QStringLiteral("discovery/subnet"), QStringLiteral("192.168.100.0/24")); }
    int adbPort() const { return intValue(QStringLiteral("discovery/adbPort"), 5555); }
    int quickRefreshSeconds() const { return intValue(QStringLiteral("discovery/quickRefreshSeconds"), 4); }
    int fullScanSeconds() const { return intValue(QStringLiteral("discovery/fullScanSeconds"), 45); }
    int scanConcurrency() const { return intValue(QStringLiteral("discovery/scanConcurrency"), 64); }
    int scanTimeoutMs() const { return intValue(QStringLiteral("discovery/scanTimeoutMs"), 800); }
    bool autoConnect() const { return boolValue(QStringLiteral("discovery/autoConnect"), true); }
    bool autoMirror() const { return boolValue(QStringLiteral("discovery/autoMirror"), true); }
    bool useMdns() const { return boolValue(QStringLiteral("discovery/useMdns"), true); }
    bool useArp() const { return boolValue(QStringLiteral("discovery/useArp"), true); }
    bool adaptiveScan() const { return boolValue(QStringLiteral("discovery/adaptive"), true); }

    // ---- adb ----
    QString adbPath() const { return stringValue(QStringLiteral("adb/path"), QString()); }
    int adbConcurrency() const { return intValue(QStringLiteral("adb/concurrency"), 8); }
    int adbTimeoutMs() const { return intValue(QStringLiteral("adb/timeoutMs"), 15000); }
    int connectConcurrency() const { return intValue(QStringLiteral("adb/connectConcurrency"), 8); }
    int connectTimeoutMs() const { return intValue(QStringLiteral("adb/connectTimeoutMs"), 6000); }

    // ---- mirroring ----
    int maxSimultaneousMirrorStarts() const { return intValue(QStringLiteral("mirror/maxConcurrentStarts"), 4); }
    int mirrorStartTimeoutMs() const { return intValue(QStringLiteral("mirror/startTimeoutMs"), 20000); }
    QString qualityPreset() const { return stringValue(QStringLiteral("mirror/preset"), QStringLiteral("balanced")); }
    int maxSize() const { return intValue(QStringLiteral("mirror/maxSize"), 800); }
    int bitRate() const { return intValue(QStringLiteral("mirror/bitRate"), 4000000); }
    int maxFps() const { return intValue(QStringLiteral("mirror/maxFps"), 30); }
    bool adaptiveQuality() const { return boolValue(QStringLiteral("mirror/adaptiveQuality"), true); }
    bool normalizeResolution() const { return boolValue(QStringLiteral("mirror/normalizeResolution"), true); }
    QString normalizedSize() const { return stringValue(QStringLiteral("mirror/normalizedSize"), QStringLiteral("1080x2220")); }
    QString normalizedDensity() const { return stringValue(QStringLiteral("mirror/normalizedDensity"), QStringLiteral("480")); }
    bool autoReconnect() const { return boolValue(QStringLiteral("mirror/autoReconnect"), true); }
    int reconnectMaxAttempts() const { return intValue(QStringLiteral("mirror/reconnectMaxAttempts"), 0); }    // 0 = unlimited

    // ---- performance ----
    int offscreenFps() const { return intValue(QStringLiteral("perf/offscreenFps"), 2); }
    bool renderOffscreen() const { return boolValue(QStringLiteral("perf/renderOffscreen"), false); }
    int perfSampleMs() const { return intValue(QStringLiteral("perf/sampleMs"), 1000); }

    // ---- keep awake ----
    bool keepAwake() const { return boolValue(QStringLiteral("keepawake/enabled"), true); }
    bool keepAwakeReapplyOnReconnect() const { return boolValue(QStringLiteral("keepawake/reapplyOnReconnect"), true); }
    bool wakeSleepingDevices() const { return boolValue(QStringLiteral("keepawake/wakeSleeping"), true); }
    int wakeCheckSeconds() const { return intValue(QStringLiteral("keepawake/checkSeconds"), 60); }

    // ---- health ----
    bool healthMonitor() const { return boolValue(QStringLiteral("health/enabled"), true); }
    int healthIntervalSeconds() const { return intValue(QStringLiteral("health/intervalSeconds"), 45); }
    int batteryLowThreshold() const { return intValue(QStringLiteral("health/batteryLow"), 15); }
    int temperatureHighThreshold() const { return intValue(QStringLiteral("health/temperatureHigh"), 45); }

    // ---- automation ----
    int automationConcurrency() const { return intValue(QStringLiteral("automation/concurrency"), 5); }
    QString automationRunsDirectory() const;
    bool errorScreenshots() const { return boolValue(QStringLiteral("automation/errorScreenshots"), true); }

    // ---- storage ----
    QString screenshotDirectory() const;
    QString recordingDirectory() const;

    // ---- notifications ----
    bool notificationsEnabled() const { return boolValue(QStringLiteral("notify/enabled"), true); }
    bool notifyCategory(const QString &category) const { return boolValue(QStringLiteral("notify/") + category, true); }

    // ---- AI (optional) ----
    QString aiProvider() const { return stringValue(QStringLiteral("ai/provider"), QStringLiteral("none")); }
    QString aiEndpoint() const { return stringValue(QStringLiteral("ai/endpoint"), QStringLiteral("http://localhost:11434")); }
    QString aiModel() const { return stringValue(QStringLiteral("ai/model"), QStringLiteral("llama3.1")); }

    // ---- ui ----
    QString tileDensity() const { return stringValue(QStringLiteral("ui/tileDensity"), QStringLiteral("normal")); }
    int tileWidth() const { return intValue(QStringLiteral("ui/tileWidth"), 190); }
    QString lastPage() const { return stringValue(QStringLiteral("ui/lastPage"), QStringLiteral("devices")); }

signals:
    void changed(const QString &key);

private:
    explicit FarmSettings(QObject *parent = nullptr);
    void ensureStorage() const;

    QString m_dataDir;
    mutable QSettings *m_settings = nullptr;
    mutable QRecursiveMutex m_mutex;    // value() is read from automation workers while the GUI saves
};

} // namespace farm

#endif // FARM_CORE_FARMSETTINGS_H
