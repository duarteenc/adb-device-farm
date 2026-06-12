#ifndef FARM_DEVICETILE_H
#define FARM_DEVICETILE_H

#include <QSize>
#include <QWidget>

#include "QtScrcpyCore.h"

class QYUVOpenGLWidget;
class QLabel;
class QMouseEvent;
class QWheelEvent;
class QKeyEvent;

/**
 * One device view in the farm grid: an OpenGL YUV renderer with the device's
 * number / model / IP overlaid on top of the screen (GenFarmer-style).
 *
 * Registers as a qsc::DeviceObserver so the core delivers decoded frames to
 * onFrame() on the GUI thread -> QYUVOpenGLWidget::updateTextures() -> GPU.
 *
 * Tiles keep a fixed width (size slider, floored) so a phone is always readable;
 * the grid scrolls rather than shrinking tiles below that floor.
 */
class DeviceTile
    : public QWidget
    , public qsc::DeviceObserver
{
    Q_OBJECT
public:
    explicit DeviceTile(const QString &serial, QWidget *parent = nullptr);
    ~DeviceTile() override;

    const QString &serial() const { return m_serial; }
    void setNumber(int number);
    void setModel(const QString &model);
    void setStatusText(const QString &text);
    void setSelected(bool selected);
    void setUnderControl(bool on);
    void setTileWidth(int width);

    QSize videoFrameSize() const;
    QSize videoShowSize() const;

    // qsc::DeviceObserver
    void onFrame(int width, int height, uint8_t *dataY, uint8_t *dataU, uint8_t *dataV,
                 int linesizeY, int linesizeU, int linesizeV) override;
    void updateFPS(quint32 fps) override;

signals:
    void clicked(const QString &serial);
    void doubleClicked(const QString &serial);
    void mouseInput(const QString &serial, QMouseEvent *event);
    void wheelInput(const QString &serial, QWheelEvent *event);
    void keyInput(const QString &serial, QKeyEvent *event);
    void fpsUpdated(quint32 fps);    // internal: decoder thread -> GUI thread

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void refreshOverlay();
    void applyBorder();

    QString m_serial;
    QString m_ip;
    QString m_model;
    QString m_statusText;
    int m_number = 0;
    quint32 m_fps = 0;
    bool m_selected = false;
    bool m_underControl = false;
    int m_tileWidth = 190;
    double m_frameAspect = 2.0;    // height/width of the device frame (auto from video)

    QYUVOpenGLWidget *m_video = nullptr;
    QWidget *m_overlay = nullptr;     // sits on top of the video, anchored to the top
    QLabel *m_numLabel = nullptr;
    QLabel *m_modelLabel = nullptr;
    QLabel *m_ipLabel = nullptr;
    QLabel *m_fpsLabel = nullptr;
};

#endif // FARM_DEVICETILE_H
