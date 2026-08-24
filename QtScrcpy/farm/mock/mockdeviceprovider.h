#ifndef FARM_MOCK_MOCKDEVICEPROVIDER_H
#define FARM_MOCK_MOCKDEVICEPROVIDER_H

#include <QByteArray>
#include <QHash>
#include <QList>
#include <QMutex>
#include <QObject>
#include <QSet>
#include <QTimer>

#include "QtScrcpyCore.h"

namespace farm {

/**
 * Test/mock device layer — NOT used in production runs.
 *
 * `--mock-devices N` populates the registry with N simulated phones (numbered,
 * grouped in boxes of 20, random battery/health) and streams synthetic YUV420
 * frames (moving pattern + device number) to whichever DeviceObservers attach,
 * from a worker thread at a configurable fps. It exercises the grid, selection,
 * viewport throttling, batch UI and the automation engine's device fan-out at
 * 50/100/200/300 devices without any ADB. Benchmarks taken in mock mode are
 * reported as mock numbers, never as real-device numbers.
 */
class MockDeviceProvider : public QObject
{
    Q_OBJECT
public:
    static MockDeviceProvider &instance();

    void start(int count, int fps = 10, int width = 270, int height = 585);
    void stop();
    bool isActive() const { return m_active; }
    int count() const { return m_count; }
    static bool isMockId(const QString &id) { return id.startsWith(QLatin1String("mock-")); }

    void attach(const QString &id, qsc::DeviceObserver *observer);
    void detach(const QString &id, qsc::DeviceObserver *observer);
    void setStreaming(const QString &id, bool on);
    /// Start/stop synthetic streams on every mock device (what Auto Mirror does for real ones).
    void streamAll(bool on);
    bool isStreaming(const QString &id) const { return m_streaming.contains(id); }
    /// Simulate a device dropping and coming back (for reconnect UI testing).
    void simulateDisconnect(const QString &id, int returnAfterMs);

private:
    explicit MockDeviceProvider(QObject *parent = nullptr);
    void tick();

    struct Frame
    {
        QByteArray y;
        QByteArray u;
        QByteArray v;
    };
    void renderFrame(int index, int phase, Frame &frame) const;

    QTimer m_timer;
    bool m_active = false;
    int m_count = 0;
    int m_fps = 10;
    int m_width = 270;
    int m_height = 585;
    int m_phase = 0;
    QHash<QString, QList<qsc::DeviceObserver *>> m_observers;
    QSet<QString> m_streaming;
    QList<Frame> m_frames;
};

} // namespace farm

#endif // FARM_MOCK_MOCKDEVICEPROVIDER_H
