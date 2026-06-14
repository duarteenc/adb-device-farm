#ifndef FARM_FOCUSPANEL_H
#define FARM_FOCUSPANEL_H

#include <functional>

#include <QList>
#include <QWidget>

#include "QtScrcpyCore.h"

class QYUVOpenGLWidget;
class QLabel;
class QMouseEvent;
class QWheelEvent;
class QKeyEvent;

/**
 * Embedded "host mode": a large interactive mirror of one device plus a control
 * column, shown inside the main window between the control panel and the grid
 * (GenFarmer-style). Showing it pushes the grid to the right.
 *
 * Registers as an ADDITIONAL qsc::DeviceObserver on the device, so its grid tile
 * keeps mirroring too. One device at a time; double-clicking another tile
 * re-points it. detach() just stops observing — it never disconnects the device.
 */
class FocusPanel
    : public QWidget
    , public qsc::DeviceObserver
{
    Q_OBJECT
public:
    explicit FocusPanel(QWidget *parent = nullptr);
    ~FocusPanel() override;

    void showDevice(const QString &serial, const QString &title);
    void detach();
    void setHostHeight(int height);    // target phone height in px (size slider)
    void setTargets(const QList<QString> &serials);    // devices the host controls
    const QString &serial() const { return m_serial; }

    // qsc::DeviceObserver
    void onFrame(int width, int height, uint8_t *dataY, uint8_t *dataU, uint8_t *dataV,
                 int linesizeY, int linesizeU, int linesizeV) override;

signals:
    void closed(const QString &serial);
    void adbControllerRequested(const QString &serial);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void unbindDevice();
    void updateSizes();    // resize panel/mirror to the chosen host height and aspect
    void fitVideo();       // place the video (top-aligned) inside the mirror area
    void sendToTargets(const std::function<void(qsc::IDevice *)> &fn);

    QString m_serial;
    QList<QString> m_targets;    // host + selected devices the input is broadcast to
    int m_ratioW = 0;
    int m_ratioH = 0;
    int m_hostHeight = 720;    // target phone height (size slider)
    QWidget *m_mirror = nullptr;    // container sized to the phone width
    QYUVOpenGLWidget *m_video = nullptr;
    QLabel *m_title = nullptr;
};

#endif // FARM_FOCUSPANEL_H
