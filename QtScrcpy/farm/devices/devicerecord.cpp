#include "devicerecord.h"

#include "../core/ipv4.h"

namespace farm {

QString deviceStateName(DeviceState state)
{
    switch (state) {
    case DeviceState::Unknown:
        return QStringLiteral("Unknown");
    case DeviceState::Discovered:
        return QStringLiteral("Discovered");
    case DeviceState::Connecting:
        return QStringLiteral("Connecting");
    case DeviceState::AdbOnline:
        return QStringLiteral("ADB Online");
    case DeviceState::Unauthorized:
        return QStringLiteral("Unauthorized");
    case DeviceState::Offline:
        return QStringLiteral("Offline");
    case DeviceState::Mirroring:
        return QStringLiteral("Mirroring");
    case DeviceState::Busy:
        return QStringLiteral("Busy");
    case DeviceState::Error:
        return QStringLiteral("Error");
    case DeviceState::Reconnecting:
        return QStringLiteral("Reconnecting");
    }
    return QStringLiteral("Unknown");
}

QString connectionTypeName(ConnectionType type)
{
    switch (type) {
    case ConnectionType::Usb:
        return QStringLiteral("USB");
    case ConnectionType::WifiAdb:
        return QStringLiteral("WiFi ADB");
    case ConnectionType::Mdns:
        return QStringLiteral("mDNS Wireless Debugging");
    case ConnectionType::Known:
        return QStringLiteral("Known Device");
    case ConnectionType::Unknown:
        break;
    }
    return QStringLiteral("Unknown");
}

bool deviceStateIsOnline(DeviceState state)
{
    switch (state) {
    case DeviceState::AdbOnline:
    case DeviceState::Mirroring:
    case DeviceState::Busy:
        return true;
    default:
        return false;
    }
}

QString DeviceRecord::host() const
{
    return ipv4::hostOf(id);
}

bool DeviceRecord::isTcp() const
{
    return ipv4::isTcpEndpoint(id);
}

} // namespace farm
