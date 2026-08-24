#include "devicegrid.h"

#include <algorithm>

#include <QKeySequence>
#include <QResizeEvent>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QKeyEvent>
#include <QMimeData>
#include <QMouseEvent>
#include <QRubberBand>
#include <QScrollBar>
#include <QUrl>

#include "farmtheme.h"

namespace farm {

namespace {
constexpr int kSpacing = 8;
constexpr int kMargin = 10;
} // namespace

DeviceGrid::DeviceGrid(QWidget *parent)
    : QScrollArea(parent)
{
    setObjectName(QStringLiteral("deviceGrid"));
    setWidgetResizable(false);
    setFrameShape(QFrame::NoFrame);
    setAcceptDrops(true);
    setFocusPolicy(Qt::StrongFocus);
    m_host = new QWidget;
    m_host->setObjectName(QStringLiteral("gridHost"));
    m_host->setStyleSheet(QStringLiteral("#gridHost{background:%1;}").arg(theme::background().name()));
    m_host->installEventFilter(this);
    m_host->setAcceptDrops(true);
    setWidget(m_host);
    m_rubberBand = new QRubberBand(QRubberBand::Rectangle, m_host);
    viewport()->installEventFilter(this);

    m_relayoutTimer.setSingleShot(true);
    m_relayoutTimer.setInterval(0);
    connect(&m_relayoutTimer, &QTimer::timeout, this, &DeviceGrid::relayout);
    m_priorityTimer.setSingleShot(true);
    m_priorityTimer.setInterval(60);
    connect(&m_priorityTimer, &QTimer::timeout, this, &DeviceGrid::updateRenderPriorities);
}

DeviceTile *DeviceGrid::ensureTile(const QString &id)
{
    if (DeviceTile *t = m_tiles.value(id, nullptr)) {
        return t;
    }
    auto *tile = new DeviceTile(id, m_host);
    tile->setTileWidth(m_tileWidth);
    tile->setDensity(m_density);
    tile->setControllable(m_controllable);
    tile->hide();
    connect(tile, &DeviceTile::clicked, this, &DeviceGrid::onTileClicked);
    connect(tile, &DeviceTile::doubleClicked, this, &DeviceGrid::tileDoubleClicked);
    connect(tile, &DeviceTile::mouseInput, this, &DeviceGrid::tileMouse);
    connect(tile, &DeviceTile::wheelInput, this, &DeviceGrid::tileWheel);
    connect(tile, &DeviceTile::keyInput, this, &DeviceGrid::tileKey);
    connect(tile, &DeviceTile::contextMenuRequested, this, &DeviceGrid::contextMenuRequested);
    connect(tile, &DeviceTile::filesDropped, this, &DeviceGrid::filesDropped);
    m_tiles.insert(id, tile);
    return tile;
}

void DeviceGrid::removeTile(const QString &id)
{
    DeviceTile *tile = m_tiles.take(id);
    if (!tile) {
        return;
    }
    m_order.removeAll(id);
    if (m_selected.remove(id)) {
        emitSelection();
    }
    tile->hide();
    tile->deleteLater();
    scheduleRelayout();
}

void DeviceGrid::setOrder(const QStringList &ids)
{
    if (ids == m_order) {
        return;
    }
    m_order = ids;
    const QSet<QString> visible(ids.begin(), ids.end());
    for (auto it = m_tiles.begin(); it != m_tiles.end(); ++it) {
        if (!visible.contains(it.key())) {
            it.value()->hide();
            it.value()->setRenderPriority(DeviceTile::RenderPriority::Hidden);
        }
    }
    scheduleRelayout();
}

void DeviceGrid::setTileWidth(int width)
{
    m_tileWidth = width;
    for (DeviceTile *t : m_tiles) {
        t->setTileWidth(width);
    }
    scheduleRelayout();
}

void DeviceGrid::setDensity(DeviceTile::Density density)
{
    m_density = density;
    for (DeviceTile *t : m_tiles) {
        t->setDensity(density);
    }
    scheduleRelayout();
}

void DeviceGrid::setControllable(bool on)
{
    m_controllable = on;
    for (DeviceTile *t : m_tiles) {
        t->setControllable(on);
    }
}

void DeviceGrid::setFocusedId(const QString &id)
{
    if (DeviceTile *old = m_tiles.value(m_focusedId, nullptr)) {
        old->setUnderControl(false);
    }
    m_focusedId = id;
    if (DeviceTile *t = m_tiles.value(id, nullptr)) {
        t->setUnderControl(true);
    }
    updateRenderPriorities();
}

void DeviceGrid::scheduleRelayout()
{
    if (!m_relayoutTimer.isActive()) {
        m_relayoutTimer.start();
    }
}

void DeviceGrid::relayout()
{
    const int avail = viewport()->width() - 2 * kMargin;
    const int per = m_tileWidth + kSpacing;
    m_columns = std::max(1, (avail + kSpacing) / per);
    int x = kMargin;
    int y = kMargin;
    int col = 0;
    int rowHeight = 0;
    int maxY = kMargin;
    for (const QString &id : m_order) {
        DeviceTile *tile = m_tiles.value(id, nullptr);
        if (!tile) {
            continue;
        }
        if (col >= m_columns) {
            col = 0;
            x = kMargin;
            y += rowHeight + kSpacing;
            rowHeight = 0;
        }
        tile->move(x, y);
        tile->show();
        rowHeight = std::max(rowHeight, tile->height());
        maxY = std::max(maxY, y + tile->height());
        x += per;
        ++col;
    }
    m_host->resize(std::max(viewport()->width(), 2 * kMargin + m_columns * per), maxY + kMargin);
    updateRenderPriorities();
    emit layoutChanged();
}

void DeviceGrid::updateRenderPriorities()
{
    const QRect view(QPoint(horizontalScrollBar()->value(), verticalScrollBar()->value()), viewport()->size());
    // One screen of margin above/below keeps scrolling smooth without paying for the whole wall.
    const QRect near = view.adjusted(0, -view.height(), 0, view.height());
    for (const QString &id : m_order) {
        DeviceTile *tile = m_tiles.value(id, nullptr);
        if (!tile) {
            continue;
        }
        const QRect g = tile->geometry();
        DeviceTile::RenderPriority p;
        if (id == m_focusedId) {
            p = DeviceTile::RenderPriority::Focused;
        } else if (g.intersects(view)) {
            p = DeviceTile::RenderPriority::Visible;
        } else if (g.intersects(near)) {
            p = DeviceTile::RenderPriority::Offscreen;
        } else {
            p = DeviceTile::RenderPriority::Hidden;
        }
        const DeviceTile::RenderPriority old = tile->renderPriority();
        tile->setRenderPriority(p);
        if ((p == DeviceTile::RenderPriority::Visible || p == DeviceTile::RenderPriority::Focused)
            && (old == DeviceTile::RenderPriority::Offscreen || old == DeviceTile::RenderPriority::Hidden) && tile->isStale()) {
            emit replayRequested(id);
        }
    }
}

void DeviceGrid::resizeEvent(QResizeEvent *event)
{
    QScrollArea::resizeEvent(event);
    scheduleRelayout();
}

void DeviceGrid::scrollContentsBy(int dx, int dy)
{
    QScrollArea::scrollContentsBy(dx, dy);
    if (!m_priorityTimer.isActive()) {
        m_priorityTimer.start();
    }
}

// ---------------------------------------------------------------- selection

QStringList DeviceGrid::selection() const
{
    QStringList list;
    for (const QString &id : m_order) {
        if (m_selected.contains(id)) {
            list << id;
        }
    }
    // Selected but currently filtered out still count.
    for (const QString &id : m_selected) {
        if (!list.contains(id)) {
            list << id;
        }
    }
    return list;
}

void DeviceGrid::emitSelection()
{
    for (auto it = m_tiles.begin(); it != m_tiles.end(); ++it) {
        it.value()->setSelected(m_selected.contains(it.key()));
    }
    emit selectionChanged();
}

void DeviceGrid::setSelection(const QSet<QString> &ids)
{
    m_selected = ids;
    emitSelection();
}

void DeviceGrid::select(const QString &id, bool on)
{
    if (on) {
        m_selected.insert(id);
    } else {
        m_selected.remove(id);
    }
    emitSelection();
}

void DeviceGrid::selectAll()
{
    m_selected = QSet<QString>(m_order.begin(), m_order.end());
    emitSelection();
}

void DeviceGrid::clearSelection()
{
    if (m_selected.isEmpty()) {
        return;
    }
    m_selected.clear();
    emitSelection();
}

void DeviceGrid::invertSelection()
{
    QSet<QString> next;
    for (const QString &id : m_order) {
        if (!m_selected.contains(id)) {
            next.insert(id);
        }
    }
    m_selected = next;
    emitSelection();
}

void DeviceGrid::scrollToTile(const QString &id)
{
    if (DeviceTile *t = m_tiles.value(id, nullptr)) {
        ensureWidgetVisible(t, 20, 20);
    }
}

QString DeviceGrid::tileAt(const QPoint &hostPoint) const
{
    for (const QString &id : m_order) {
        DeviceTile *t = m_tiles.value(id, nullptr);
        if (t && t->isVisible() && t->geometry().contains(hostPoint)) {
            return id;
        }
    }
    return QString();
}

void DeviceGrid::onTileClicked(const QString &id, Qt::KeyboardModifiers modifiers)
{
    if (modifiers & Qt::ShiftModifier) {
        const int a = static_cast<int>(m_order.indexOf(m_anchor));
        const int b = static_cast<int>(m_order.indexOf(id));
        if (a >= 0 && b >= 0) {
            if (!(modifiers & Qt::ControlModifier)) {
                m_selected.clear();
            }
            for (int i = std::min(a, b); i <= std::max(a, b); ++i) {
                m_selected.insert(m_order.at(i));
            }
        } else {
            m_selected.insert(id);
        }
    } else if (modifiers & Qt::ControlModifier) {
        if (m_selected.contains(id)) {
            m_selected.remove(id);
        } else {
            m_selected.insert(id);
        }
        m_anchor = id;
    } else {
        // Plain click toggles membership (farm operators select-by-clicking).
        if (m_selected.contains(id)) {
            m_selected.remove(id);
        } else {
            m_selected.insert(id);
        }
        m_anchor = id;
    }
    emitSelection();
    emit tileClicked(id);
}

void DeviceGrid::applyRubberSelection(const QRect &rect, bool additive)
{
    if (!additive) {
        m_selected.clear();
    }
    for (const QString &id : m_order) {
        DeviceTile *t = m_tiles.value(id, nullptr);
        if (t && t->isVisible() && t->geometry().intersects(rect)) {
            m_selected.insert(id);
        }
    }
    emitSelection();
}

bool DeviceGrid::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == viewport() && event->type() == QEvent::Resize) {
        scheduleRelayout();
        return QScrollArea::eventFilter(watched, event);
    }
    if (watched != m_host) {
        return QScrollArea::eventFilter(watched, event);
    }
    switch (event->type()) {
    case QEvent::MouseButtonPress: {
        auto *me = static_cast<QMouseEvent *>(event);
        setFocus();
        if (me->button() == Qt::LeftButton) {
            m_rubberOrigin = me->position().toPoint();
            m_dragging = false;
            const QString hit = tileAt(m_rubberOrigin);
            m_pressedOnSelected = !hit.isEmpty() && m_selected.contains(hit);
        } else if (me->button() == Qt::RightButton) {
            const QString hit = tileAt(me->position().toPoint());
            emit contextMenuRequested(hit, me->globalPosition().toPoint());
        }
        break;
    }
    case QEvent::MouseMove: {
        auto *me = static_cast<QMouseEvent *>(event);
        if (!(me->buttons() & Qt::LeftButton) || m_pressedOnSelected) {
            break;
        }
        const QPoint p = me->position().toPoint();
        if (!m_dragging && (p - m_rubberOrigin).manhattanLength() > 6) {
            m_dragging = true;
            m_rubberBand->setGeometry(QRect(m_rubberOrigin, QSize()));
            m_rubberBand->show();
            m_rubberBand->raise();
        }
        if (m_dragging) {
            const QRect r = QRect(m_rubberOrigin, p).normalized();
            m_rubberBand->setGeometry(r);
            QSet<QString> now;
            for (const QString &id : m_order) {
                DeviceTile *t = m_tiles.value(id, nullptr);
                if (t && t->isVisible() && t->geometry().intersects(r)) {
                    now.insert(id);
                }
            }
            if (now != m_underRubber) {
                for (const QString &id : m_underRubber) {
                    if (!now.contains(id)) {
                        if (DeviceTile *t = m_tiles.value(id, nullptr)) {
                            t->setSelectionPreview(false);
                        }
                    }
                }
                for (const QString &id : now) {
                    if (!m_underRubber.contains(id)) {
                        if (DeviceTile *t = m_tiles.value(id, nullptr)) {
                            t->setSelectionPreview(true);
                        }
                    }
                }
                m_underRubber = now;
            }
            // auto-scroll near the edges
            const QPoint vp = viewport()->mapFromGlobal(me->globalPosition().toPoint());
            if (vp.y() > viewport()->height() - 24) {
                verticalScrollBar()->setValue(verticalScrollBar()->value() + 12);
            } else if (vp.y() < 24) {
                verticalScrollBar()->setValue(verticalScrollBar()->value() - 12);
            }
        }
        break;
    }
    case QEvent::MouseButtonRelease: {
        auto *me = static_cast<QMouseEvent *>(event);
        if (me->button() != Qt::LeftButton) {
            break;
        }
        if (m_dragging) {
            const QRect r = m_rubberBand->geometry();
            m_rubberBand->hide();
            m_dragging = false;
            for (const QString &id : m_underRubber) {
                if (DeviceTile *t = m_tiles.value(id, nullptr)) {
                    t->setSelectionPreview(false);
                }
            }
            m_underRubber.clear();
            applyRubberSelection(r, me->modifiers().testFlag(Qt::ControlModifier));
        } else {
            const QString hit = tileAt(me->position().toPoint());
            if (!hit.isEmpty()) {
                onTileClicked(hit, me->modifiers());
            } else if (!m_selected.isEmpty() && !(me->modifiers() & Qt::ControlModifier)) {
                clearSelection();
            }
        }
        m_pressedOnSelected = false;
        break;
    }
    case QEvent::MouseButtonDblClick: {
        auto *me = static_cast<QMouseEvent *>(event);
        const QString hit = tileAt(me->position().toPoint());
        if (!hit.isEmpty()) {
            emit tileDoubleClicked(hit);
        }
        break;
    }
    case QEvent::DragEnter: {
        auto *de = static_cast<QDragEnterEvent *>(event);
        if (de->mimeData()->hasUrls()) {
            de->acceptProposedAction();
            return true;
        }
        break;
    }
    case QEvent::Drop: {
        auto *de = static_cast<QDropEvent *>(event);
        QStringList files;
        const QList<QUrl> urls = de->mimeData()->urls();
        for (const QUrl &u : urls) {
            if (u.isLocalFile()) {
                files << u.toLocalFile();
            }
        }
        if (!files.isEmpty()) {
            const QString hit = tileAt(de->position().toPoint());
            emit filesDropped(hit, files);
            de->acceptProposedAction();
            return true;
        }
        break;
    }
    default:
        break;
    }
    return QScrollArea::eventFilter(watched, event);
}

void DeviceGrid::dragEnterEvent(QDragEnterEvent *event)
{
    if (event->mimeData()->hasUrls()) {
        event->acceptProposedAction();
    }
}

void DeviceGrid::dropEvent(QDropEvent *event)
{
    QStringList files;
    const QList<QUrl> urls = event->mimeData()->urls();
    for (const QUrl &u : urls) {
        if (u.isLocalFile()) {
            files << u.toLocalFile();
        }
    }
    if (!files.isEmpty()) {
        emit filesDropped(QString(), files);
        event->acceptProposedAction();
    }
}

void DeviceGrid::keyPressEvent(QKeyEvent *event)
{
    if (event->matches(QKeySequence::SelectAll)) {
        selectAll();
        return;
    }
    if (event->key() == Qt::Key_Escape) {
        clearSelection();
        return;
    }
    // Arrow keys move the anchor tile and (with Shift) extend the selection.
    int idx = static_cast<int>(m_order.indexOf(m_anchor));
    if (idx < 0 && !m_order.isEmpty()) {
        idx = 0;
    }
    int next = idx;
    switch (event->key()) {
    case Qt::Key_Left:
        next = idx - 1;
        break;
    case Qt::Key_Right:
        next = idx + 1;
        break;
    case Qt::Key_Up:
        next = idx - m_columns;
        break;
    case Qt::Key_Down:
        next = idx + m_columns;
        break;
    case Qt::Key_Home:
        next = 0;
        break;
    case Qt::Key_End:
        next = static_cast<int>(m_order.size()) - 1;
        break;
    default:
        QScrollArea::keyPressEvent(event);
        return;
    }
    if (next < 0 || next >= m_order.size()) {
        return;
    }
    const QString id = m_order.at(next);
    if (event->modifiers() & Qt::ShiftModifier) {
        m_selected.insert(id);
    } else {
        m_selected = QSet<QString>{ id };
    }
    m_anchor = id;
    scrollToTile(id);
    emitSelection();
}

} // namespace farm
