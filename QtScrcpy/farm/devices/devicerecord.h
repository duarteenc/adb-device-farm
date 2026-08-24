#ifndef FARM_DEVICES_DEVICERECORD_H
#define FARM_DEVICES_DEVICERECORD_H

#include <QDateTime>
#include <QSize>
#include <QString>
#include <QVariantMap>

namespace farm {

enum class DeviceState {
    Unknown,
    Discovered,     // seen on the network / by adb but not usable yet
    Connecting,     // adb connect / server push in progress
    AdbOnline,      // adb reports "device"; not mirroring
    Unauthorized,   // needs RSA approval on the phone
    Offline,        // known device, currently unreachable
    Mirroring,      // scrcpy session live
    Busy,           // automation / batch job running
    Error,          // last operation failed (see stateMessage)
    Reconnecting,   // automatic reconnect with backoff in progress
};

enum class ConnectionType { Unknown = 0, Usb = 1, WifiAdb = 2, Mdns = 3, Known = 4 };

QString deviceStateName(DeviceState state);
QString connectionTypeName(ConnectionType type);
bool deviceStateIsOnline(DeviceState state);

/**
 * Everything the farm knows about one device. The persisted part (identity and
 * operator metadata) lives in the `devices` table; the runtime part (state,
 * health, live fps) is refreshed while the app runs.
 */
struct DeviceRecord
{
    // ---- identity (persisted) ----
    QString id;                 // ADB serial: "192.168.100.13:5555" or USB serial
    QString hwSerial;           // ro.serialno when readable (links USB <-> WiFi identities)
    QString lastIp;
    int port = 5555;
    QString model;
    QString manufacturer;
    QString androidVersion;
    int sdk = 0;
    QString friendlyName;
    int number = 0;             // logical farm number (001, 002, ...)
    QString group;
    ConnectionType connectionType = ConnectionType::Unknown;
    QDateTime firstSeen;
    QDateTime lastSeen;
    bool favorite = false;
    QString notes;
    int bitRate = 0;            // 0 = inherit global profile
    int fps = 0;
    int maxSize = 0;
    QString preset;             // "", "performance", "balanced", "quality", "focused"
    int keepAwake = -1;         // -1 inherit, 0 off, 1 on
    bool autoConnect = true;
    bool autoMirror = true;
    int pinnedOrder = 0;
    QVariantMap props;          // getprop snapshot (ro.product.*, ro.build.*)

    // ---- runtime (not persisted) ----
    DeviceState state = DeviceState::Unknown;
    QString stateMessage;
    QDateTime lastStateChange;
    QString adbState;           // raw state word from `adb devices`
    int battery = -1;
    bool charging = false;
    double temperatureC = -1;
    qint64 storageFreeMb = -1;
    int wifiRssi = 0;
    int latencyMs = -1;
    bool screenOn = true;
    bool locked = false;
    QString keepAwakeStatus;    // "Active", "Failed: ...", "" (not applied)
    int liveFps = 0;
    int reconnectAttempts = 0;
    bool automationRunning = false;
    QSize screenSize;
    qint64 uptimeSeconds = -1;
    QDateTime lastHealthCheck;

    QString displayName() const
    {
        if (!friendlyName.isEmpty()) {
            return friendlyName;
        }
        if (!model.isEmpty()) {
            return model;
        }
        return id;
    }
    QString host() const;
    bool isTcp() const;
    bool isOnline() const { return deviceStateIsOnline(state); }
    QString numberString() const { return number > 0 ? QStringLiteral("%1").arg(number, 3, 10, QLatin1Char('0')) : QString(); }
};

} // namespace farm

Q_DECLARE_METATYPE(farm::DeviceState)
Q_DECLARE_METATYPE(farm::DeviceRecord)

#endif // FARM_DEVICES_DEVICERECORD_H
