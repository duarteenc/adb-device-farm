#ifndef FARM_UI_DEVICEGRID_H
#define FARM_UI_DEVICEGRID_H

#include <QHash>
#include <QPoint>
#include <QScrollArea>
#include <QSet>
#include <QStringList>
#include <QTimer>

#include "devicetile.h"

class QRubberBand;

namespace farm {

/**
 * Scrollable wall of DeviceTiles with manual flow layout (no QGridLayout rebuilds
 * — positioning 300 tiles is a loop of setGeometry calls), rubber-band / click /
 * Ctrl / Shift selection, keyboard navigation and viewport-based render
 * priorities (visible tiles stream at full rate, off-screen ones are throttled).
 */
class DeviceGrid : public QScrollArea
{
    Q_OBJECT
public:
    explicit DeviceGrid(QWidget *parent = nullptr);

    DeviceTile *tile(const QString &id) const { return m_tiles.value(id, nullptr); }
    DeviceTile *ensureTile(const QString &id);
    void removeTile(const QString &id);
    QList<DeviceTile *> tiles() const { return m_tiles.values(); }

    void setOrder(const QStringList &ids);    // visible tiles in display order
    QStringList order() const { return m_order; }
    void setTileWidth(int width);
    int tileWidth() const { return m_tileWidth; }
    void setDensity(DeviceTile::Density density);
    DeviceTile::Density density() const { return m_density; }
    void setControllable(bool on);
    bool isControllable() const { return m_controllable; }
    void setFocusedId(const QString &id);
    QString focusedId() const { return m_focusedId; }

    QStringList selection() const;            // in display order
    QSet<QString> selectionSet() const { return m_selected; }
    void setSelection(const QSet<QString> &ids);
    void select(const QString &id, bool on);
    void selectAll();
    void clearSelection();
    void invertSelection();
    void scrollToTile(const QString &id);
    QString tileAt(const QPoint &hostPoint) const;

    void relayout();
    void scheduleRelayout();
    void updateRenderPriorities();

signals:
    void selectionChanged();
    void tileClicked(const QString &id);
    void tileDoubleClicked(const QString &id);
    void tileMouse(const QString &id, QMouseEvent *event);
    void tileWheel(const QString &id, QWheelEvent *event);
    void tileKey(const QString &id, QKeyEvent *event);
    void contextMenuRequested(const QString &id, const QPoint &globalPos);    // id empty = background
    void filesDropped(const QString &id, const QStringList &files);            // id empty = whole selection
    void replayRequested(const QString &id);                                   // stale tile scrolled into view
    void layoutChanged();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void scrollContentsBy(int dx, int dy) override;
    void keyPressEvent(QKeyEvent *event) override;
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dropEvent(QDropEvent *event) override;

private:
    void applyRubberSelection(const QRect &rect, bool additive);
    void onTileClicked(const QString &id, Qt::KeyboardModifiers modifiers);
    void emitSelection();

    QWidget *m_host = nullptr;
    QRubberBand *m_rubberBand = nullptr;
    QHash<QString, DeviceTile *> m_tiles;
    QStringList m_order;
    QSet<QString> m_selected;
    QSet<QString> m_underRubber;
    QString m_anchor;
    QString m_focusedId;
    QPoint m_rubberOrigin;
    QTimer m_relayoutTimer;
    QTimer m_priorityTimer;
    int m_tileWidth = 190;
    int m_columns = 1;
    DeviceTile::Density m_density = DeviceTile::Density::Normal;
    bool m_controllable = false;
    bool m_dragging = false;
    bool m_pressedOnSelected = false;
};

} // namespace farm

#endif // FARM_UI_DEVICEGRID_H
