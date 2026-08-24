#include "mockdeviceprovider.h"

#include <algorithm>
#include <cmath>

#include <QRandomGenerator>

#include "../core/activitylog.h"
#include "../core/farmlog.h"
#include "../devices/deviceregistry.h"

namespace farm {

MockDeviceProvider &MockDeviceProvider::instance()
{
    static MockDeviceProvider provider;
    return provider;
}

MockDeviceProvider::MockDeviceProvider(QObject *parent)
    : QObject(parent)
{
    connect(&m_timer, &QTimer::timeout, this, &MockDeviceProvider::tick);
}

void MockDeviceProvider::start(int count, int fps, int width, int height)
{
    stop();
    m_count = std::clamp(count, 1, 1000);
    m_fps = std::clamp(fps, 1, 60);
    m_width = (width / 2) * 2;
    m_height = (height / 2) * 2;
    m_active = true;
    DeviceRegistry &registry = DeviceRegistry::instance();
    const char *const models[] = { "SM-G9500", "SM-G950N", "Pixel 4a", "Redmi Note 8", "Moto G7" };
    for (int i = 0; i < m_count; ++i) {
        const QString id = QStringLiteral("mock-%1").arg(i + 1, 3, 10, QLatin1Char('0'));
        adb::AdbDeviceInfo info;
        info.serial = id;
        info.state = QStringLiteral("device");
        info.model = QString::fromLatin1(models[i % 5]);
        registry.upsertFromAdb(info);
        registry.update(id, [i](DeviceRecord &r) {
            r.lastIp = QStringLiteral("10.0.%1.%2").arg(i / 250).arg(i % 250 + 1);
            r.number = i + 1;
            r.group = QStringLiteral("Box %1").arg(i / 20 + 1);
            r.androidVersion = (i % 3 == 0) ? QStringLiteral("9") : QStringLiteral("11");
            r.sdk = (i % 3 == 0) ? 28 : 30;
            r.manufacturer = QStringLiteral("MockCorp");
            r.battery = 20 + (i * 7) % 80;
            r.charging = (i % 4) != 0;
            r.temperatureC = 28.0 + (i % 12);
            r.latencyMs = 8 + (i % 40);
            r.wifiRssi = -40 - (i % 45);
            r.screenSize = QSize(1080, 2340);
            r.keepAwakeStatus = (i % 11 == 0) ? QStringLiteral("Failed: mock vendor rejected") : QStringLiteral("Active");
        });
        if (!registry.hasGroup(QStringLiteral("Box %1").arg(i / 20 + 1))) {
            registry.createGroup(QStringLiteral("Box %1").arg(i / 20 + 1));
        }
    }
    m_frames.resize(m_count);
    m_timer.start(1000 / m_fps);
    ActivityLog::instance().info(ActivityEntry::System, QStringLiteral("Mock mode: %1 simulated devices at %2 fps (%3x%4)").arg(m_count).arg(m_fps).arg(m_width).arg(m_height));
    FarmLog::instance().info(QStringLiteral("mock"), QStringLiteral("started %1 devices").arg(m_count));
}

void MockDeviceProvider::stop()
{
    m_timer.stop();
    m_active = false;
    m_observers.clear();
    m_streaming.clear();
    m_frames.clear();
}

void MockDeviceProvider::attach(const QString &id, qsc::DeviceObserver *observer)
{
    QList<qsc::DeviceObserver *> &list = m_observers[id];
    if (!list.contains(observer)) {
        list.append(observer);
    }
}

void MockDeviceProvider::detach(const QString &id, qsc::DeviceObserver *observer)
{
    auto it = m_observers.find(id);
    if (it != m_observers.end()) {
        it.value().removeAll(observer);
    }
}

void MockDeviceProvider::setStreaming(const QString &id, bool on)
{
    if (on) {
        m_streaming.insert(id);
        DeviceRegistry::instance().setState(id, DeviceState::Mirroring);
    } else {
        m_streaming.remove(id);
        DeviceRegistry::instance().setState(id, DeviceState::AdbOnline);
    }
}

void MockDeviceProvider::simulateDisconnect(const QString &id, int returnAfterMs)
{
    const bool wasStreaming = m_streaming.remove(id);
    DeviceRegistry::instance().setState(id, DeviceState::Reconnecting, QStringLiteral("simulated drop"));
    QTimer::singleShot(returnAfterMs, this, [this, id, wasStreaming]() {
        if (!m_active) {
            return;
        }
        DeviceRegistry::instance().setState(id, wasStreaming ? DeviceState::Mirroring : DeviceState::AdbOnline);
        if (wasStreaming) {
            m_streaming.insert(id);
        }
    });
}

void MockDeviceProvider::renderFrame(int index, int phase, Frame &frame) const
{
    const int w = m_width;
    const int h = m_height;
    const int cw = w / 2;
    const int ch = h / 2;
    if (frame.y.size() != w * h) {
        frame.y.resize(w * h);
        frame.u.resize(cw * ch);
        frame.v.resize(cw * ch);
    }
    // Background gradient unique per device, a moving horizontal bar and a
    // block-digit "number" so a screenshot of the grid is verifiable.
    const int hue = (index * 37) % 255;
    uchar *y = reinterpret_cast<uchar *>(frame.y.data());
    const int bar = (phase * 6 + index * 13) % h;
    for (int row = 0; row < h; ++row) {
        const bool inBar = row >= bar && row < bar + 24;
        const uchar base = static_cast<uchar>(40 + (row * 120) / h);
        uchar *line = y + row * w;
        for (int col = 0; col < w; ++col) {
            uchar v = inBar ? 235 : base;
            // block digits: draw the device number as vertical stripes in the top area
            if (row > 40 && row < 90) {
                const int digit = (index + 1) % 100;
                const int stripe = (col / 12) % 10;
                if (stripe < digit % 10 && (col / 12) < 10) {
                    v = 200;
                }
            }
            line[col] = v;
        }
    }
    memset(frame.u.data(), 128 - hue / 4, static_cast<size_t>(cw * ch));
    memset(frame.v.data(), 128 + hue / 4, static_cast<size_t>(cw * ch));
}

void MockDeviceProvider::tick()
{
    if (!m_active) {
        return;
    }
    ++m_phase;
    for (int i = 0; i < m_count; ++i) {
        const QString id = QStringLiteral("mock-%1").arg(i + 1, 3, 10, QLatin1Char('0'));
        if (!m_streaming.contains(id)) {
            continue;
        }
        const QList<qsc::DeviceObserver *> observers = m_observers.value(id);
        if (observers.isEmpty()) {
            continue;
        }
        Frame &frame = m_frames[i];
        renderFrame(i, m_phase, frame);
        for (qsc::DeviceObserver *o : observers) {
            o->onFrame(m_width, m_height, reinterpret_cast<uint8_t *>(frame.y.data()), reinterpret_cast<uint8_t *>(frame.u.data()),
                       reinterpret_cast<uint8_t *>(frame.v.data()), m_width, m_width / 2, m_width / 2);
        }
    }
}

} // namespace farm
