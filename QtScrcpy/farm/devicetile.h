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
 * One device view in the farm grid: a compact header (number / model / IP / fps)
 * over an OpenGL YUV renderer.
 *
 * Registers itself as a qsc::DeviceObserver so the core delivers decoded frames
 * to onFrame() on the GUI thread (held under the decoder's VideoBuffer lock for
 * the call) -> QYUVOpenGLWidget::updateTextures() -> GPU. No copies.
 *
 * Tiles keep a fixed width (set by FarmWindow's size slider, with a floor) so a
 * phone is always large enough to read; the grid scrolls rather than shrinking
 * tiles below that floor.
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
    /** Fix the tile width; height follows a portrait aspect plus the header. */
    void setTileWidth(int width);

    QSize videoFrameSize() const;
    QSize videoShowSize() const;

    // qsc::DeviceObserver
    void onFrame(int width, int height, uint8_t *dataY, uint8_t *dataU, uint8_t *dataV,
                 int linesizeY, int linesizeU, int linesizeV) override;
    void updateFPS(quint32 fps) override;

signals:
    void clicked(const QString &serial);
    void mouseInput(const QString &serial, QMouseEvent *event);
    void wheelInput(const QString &serial, QWheelEvent *event);
    void keyInput(const QString &serial, QKeyEvent *event);
    void fpsUpdated(quint32 fps);    // internal: decoder thread -> GUI thread

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void refreshHeader();

    QString m_serial;
    QString m_ip;
    QString m_model;
    QString m_statusText;
    int m_number = 0;
    quint32 m_fps = 0;
    bool m_selected = false;
    int m_tileWidth = 190;

    QYUVOpenGLWidget *m_video = nullptr;
    QWidget *m_header = nullptr;
    QLabel *m_numLabel = nullptr;
    QLabel *m_modelLabel = nullptr;
    QLabel *m_ipLabel = nullptr;
    QLabel *m_fpsLabel = nullptr;
};

#endif // FARM_DEVICETILE_H
