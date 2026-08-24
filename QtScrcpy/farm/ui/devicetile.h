#ifndef FARM_UI_DEVICETILE_H
#define FARM_UI_DEVICETILE_H

#include <QElapsedTimer>
#include <QSize>
#include <QWidget>

#include "QtScrcpyCore.h"
#include "devices/devicerecord.h"

class QYUVOpenGLWidget;
class QLabel;
class QMouseEvent;
class QWheelEvent;
class QKeyEvent;
class QPropertyAnimation;

namespace farm {

/**
 * One device in the grid: a YUV OpenGL renderer with an overlay (number, name,
 * IP, battery, fps/latency, state badge, group colour) and a connection-state
 * underlay when there is no live stream.
 *
 * Render priority (set by DeviceGrid from the scroll viewport) drives the
 * latest-frame-wins throttling: Visible/Focused tiles upload every frame,
 * Offscreen tiles upload at most Settings › Performance › off-screen fps and
 * Hidden tiles upload nothing. The decoder keeps running so control is never
 * interrupted; when a stale tile scrolls back into view the last frame is
 * replayed immediately.
 */
class DeviceTile
    : public QWidget
    , public qsc::DeviceObserver
{
    Q_OBJECT
    Q_PROPERTY(int spinnerRotation READ spinnerRotation WRITE setSpinnerRotation)
public:
    enum class Density { Tiny, Compact, Normal, Large };
    enum class RenderPriority { Focused, Visible, Offscreen, Hidden };

    explicit DeviceTile(const QString &id, QWidget *parent = nullptr);
    ~DeviceTile() override;

    const QString &id() const { return m_id; }
    void setRecord(const DeviceRecord &record);
    const DeviceRecord &record() const { return m_record; }

    void setDensity(Density density);
    Density density() const { return m_density; }
    void setTileWidth(int width);
    int tileWidth() const { return m_tileWidth; }

    void setSelected(bool selected);
    bool isSelected() const { return m_selected; }
    void setSelectionPreview(bool preview);
    void setUnderControl(bool on);
    void setControllable(bool on);    // mouse drives the device (on) vs grid selection (off)
    void setStreaming(bool streaming);
    bool isStreaming() const { return m_streaming; }
    void setRenderPriority(RenderPriority priority);
    RenderPriority renderPriority() const { return m_priority; }
    bool isStale() const { return m_stale; }
    void setHighlight(bool on);       // search hit

    QSize videoFrameSize() const;
    QSize videoShowSize() const;
    QWidget *videoWidget() const;

    int spinnerRotation() const { return m_spinnerRotation; }
    void setSpinnerRotation(int rotation) { m_spinnerRotation = rotation; }

    // qsc::DeviceObserver
    void onFrame(int width, int height, uint8_t *dataY, uint8_t *dataU, uint8_t *dataV, int linesizeY, int linesizeU, int linesizeV) override;
    void updateFPS(quint32 fps) override;

signals:
    void clicked(const QString &id, Qt::KeyboardModifiers modifiers);
    void doubleClicked(const QString &id);
    void mouseInput(const QString &id, QMouseEvent *event);
    void wheelInput(const QString &id, QWheelEvent *event);
    void keyInput(const QString &id, QKeyEvent *event);
    void contextMenuRequested(const QString &id, const QPoint &globalPos);
    void filesDropped(const QString &id, const QStringList &files);
    void fpsUpdated(quint32 fps);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void contextMenuEvent(QContextMenuEvent *event) override;
    void paintEvent(QPaintEvent *event) override;
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dropEvent(QDropEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private:
    void refreshOverlay();
    void applyBorder();
    void updateTextSelectionColor();
    void layoutOverlay();
    void ensureVideo();      // the OpenGL widget is created on the first frame only
    void releaseVideo();
    void placeChildren();

    QString m_id;
    DeviceRecord m_record;
    Density m_density = Density::Normal;
    RenderPriority m_priority = RenderPriority::Visible;
    quint32 m_fps = 0;
    bool m_selected = false;
    bool m_preview = false;
    bool m_underControl = false;
    bool m_streaming = false;
    bool m_stale = false;
    bool m_highlight = false;
    bool m_hasFrame = false;
    bool m_controllable = false;
    int m_tileWidth = 190;
    double m_frameAspect = 2.0;
    int m_spinnerRotation = 0;
    qint64 m_lastUploadMs = 0;
    qint64 m_frameArrivedNs = 0;
    QElapsedTimer m_clock;

    QYUVOpenGLWidget *m_video = nullptr;
    QWidget *m_overlay = nullptr;
    QLabel *m_numLabel = nullptr;
    QLabel *m_nameLabel = nullptr;
    QLabel *m_ipLabel = nullptr;
    QLabel *m_metaLabel = nullptr;
    QLabel *m_stateBadge = nullptr;
    QLabel *m_connBadge = nullptr;
    QPropertyAnimation *m_spinner = nullptr;
};

} // namespace farm

#endif // FARM_UI_DEVICETILE_H
