#ifndef FARM_UI_FOCUSPANEL_H
#define FARM_UI_FOCUSPANEL_H

#include <functional>

#include <QList>
#include <QSize>
#include <QWidget>

#include "QtScrcpyCore.h"

class QYUVOpenGLWidget;
class QLabel;
class QCheckBox;
class QComboBox;
class QLineEdit;
class QMouseEvent;
class QWheelEvent;
class QKeyEvent;

namespace farm {

/**
 * Host / MASTER panel: a large interactive mirror of one device whose input is
 * broadcast to the FOLLOWERS (the current selection or, in Control-All mode,
 * every mirrored device).
 *
 *   Sync Mouse / Keyboard / Gestures (wheel) / Navigation   ON/OFF
 *   Coordinates: Raw | Normalized | Aspect-aware
 *
 * Raw sends the master's frame size (the scrcpy server scales proportionally),
 * Normalized maps x/masterW, y/masterH onto every follower's own frame size, and
 * Aspect-aware letterboxes the master's aspect inside followers with a different
 * ratio so taps land on the same relative spot.
 *
 * Every input event is also reported through inputRecorded() for the macro recorder.
 */
class FocusPanel
    : public QWidget
    , public qsc::DeviceObserver
{
    Q_OBJECT
public:
    enum class CoordinateMode { Raw, Normalized, AspectAware };

    explicit FocusPanel(QWidget *parent = nullptr);
    ~FocusPanel() override;

    void showDevice(const QString &id, const QString &title);
    void detach();
    void setHostHeight(int height);
    void setFollowers(const QStringList &ids);    // excluding the master
    QStringList followers() const { return m_followers; }
    const QString &serial() const { return m_serial; }
    bool syncMouse() const;
    bool syncKeyboard() const;
    bool syncGestures() const;
    bool syncNavigation() const;
    CoordinateMode coordinateMode() const;
    QSize frameSize() const;

    // qsc::DeviceObserver
    void onFrame(int width, int height, uint8_t *dataY, uint8_t *dataU, uint8_t *dataV, int linesizeY, int linesizeU, int linesizeV) override;

signals:
    void closed(const QString &id);
    void consoleRequested(const QString &id);
    void installApkRequested(const QString &id);
    void screenshotRequested(const QStringList &ids);
    void textRequested(const QStringList &ids, const QString &text);
    void templatesRequested();
    void recorderToggled(bool recording);
    /// kind: "tap" (x,y normalised 0..1), "swipe", "key" (keycode name), "text", "nav" (back/home/recent)
    void inputRecorded(const QString &kind, const QVariantMap &data);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void unbindDevice();
    void updateSizes();
    void fitVideo();
    QStringList targets(bool includeMaster) const;
    void forEachTarget(bool includeMaster, const std::function<void(qsc::IDevice *, const QString &)> &fn);
    void broadcastMouse(QMouseEvent *event);
    void broadcastWheel(QWheelEvent *event);
    void broadcastKey(QKeyEvent *event);
    void navigation(const std::function<void(qsc::IDevice *)> &fn, const QString &name);
    QPointF mapForFollower(const QPointF &pos, const QSize &masterFrame, const QSize &followerFrame) const;

    QString m_serial;
    QStringList m_followers;
    int m_ratioW = 0;
    int m_ratioH = 0;
    int m_hostHeight = 720;
    QWidget *m_mirror = nullptr;
    QYUVOpenGLWidget *m_video = nullptr;
    QLabel *m_title = nullptr;
    QLabel *m_followerLabel = nullptr;
    QCheckBox *m_syncMouse = nullptr;
    QCheckBox *m_syncKeyboard = nullptr;
    QCheckBox *m_syncGestures = nullptr;
    QCheckBox *m_syncNavigation = nullptr;
    QComboBox *m_coordMode = nullptr;
    QLineEdit *m_textEdit = nullptr;
    bool m_recording = false;
    QPointF m_pressPos;
    qint64 m_pressMs = 0;
};

} // namespace farm

#endif // FARM_UI_FOCUSPANEL_H
