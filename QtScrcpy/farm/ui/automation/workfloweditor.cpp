#include "workfloweditor.h"

#include <algorithm>

#include <QDragMoveEvent>
#include <QDropEvent>
#include <QKeySequence>
#include <QLineF>
#include <QPainterPath>
#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QDrag>
#include <QDragEnterEvent>
#include <QFileDialog>
#include <QFormLayout>
#include <QGraphicsSceneMouseEvent>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QShortcut>
#include <QSpinBox>
#include <QStyleOptionGraphicsItem>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QWheelEvent>

#include "automation/nodecatalog.h"
#include "ui/farmtheme.h"

namespace farm {

namespace {
const char *kNodeMime = "application/x-farm-node-type";

QColor categoryColor(const QString &category)
{
    if (category == QLatin1String("Flow")) {
        return QColor(0x64, 0x74, 0x8b);
    }
    if (category == QLatin1String("Device")) {
        return QColor(0x0e, 0xa5, 0xe9);
    }
    if (category == QLatin1String("Interaction")) {
        return QColor(0x22, 0xc5, 0x5e);
    }
    if (category == QLatin1String("Application")) {
        return QColor(0xa8, 0x55, 0xf7);
    }
    if (category == QLatin1String("Timing")) {
        return QColor(0xf5, 0x9e, 0x0b);
    }
    if (category == QLatin1String("Logic")) {
        return QColor(0xec, 0x48, 0x99);
    }
    if (category == QLatin1String("Variables")) {
        return QColor(0x14, 0xb8, 0xa6);
    }
    if (category == QLatin1String("ADB")) {
        return QColor(0xf9, 0x73, 0x16);
    }
    if (category == QLatin1String("Screen")) {
        return QColor(0x3b, 0x82, 0xf6);
    }
    if (category == QLatin1String("Files")) {
        return QColor(0x84, 0xcc, 0x16);
    }
    return QColor(0x94, 0xa3, 0xb8);
}

QString summarize(const WorkflowNode &n)
{
    const NodeSpec spec = NodeCatalog::spec(n.type);
    QStringList parts;
    for (const ParamSpec &p : spec.params) {
        const QVariant v = n.params.value(p.key);
        if (!v.isValid() || v.toString().isEmpty() || p.type == QLatin1String("bool")) {
            continue;
        }
        QString s = v.toString();
        if (p.type == QLatin1String("file") || p.type == QLatin1String("image")) {
            s = s.section(QLatin1Char('/'), -1).section(QLatin1Char('\\'), -1);
        }
        parts << s.left(24);
        if (parts.size() >= 2) {
            break;
        }
    }
    return parts.join(QStringLiteral("  ·  "));
}
} // namespace

// ================================================================= NodeItem

NodeItem::NodeItem(const WorkflowNode &node, WorkflowScene *scene)
    : m_scene(scene)
{
    setFlags(ItemIsMovable | ItemIsSelectable | ItemSendsGeometryChanges);
    setZValue(1);
    setNode(node);
    setPos(node.pos);
}

void NodeItem::setNode(const WorkflowNode &node)
{
    m_node = node;
    const NodeSpec spec = NodeCatalog::spec(node.type);
    m_outputs = spec.outputs;
    if (node.type == QLatin1String("logic.switch")) {
        m_outputs.clear();
        for (const QString &c : node.params.value(QStringLiteral("cases")).toString().split(QLatin1Char(','), Qt::SkipEmptyParts)) {
            m_outputs << QStringLiteral("case:") + c.trimmed();
        }
        m_outputs << QStringLiteral("default");
    }
    m_title = node.title.isEmpty() ? spec.title : node.title;
    m_subtitle = summarize(node);
    m_height = std::max<qreal>(64, 30 + 18 * std::max<qsizetype>(1, m_outputs.size()) + 8);
    prepareGeometryChange();
    update();
}

QRectF NodeItem::boundingRect() const
{
    return QRectF(-8, -4, m_width + 16, m_height + 8);
}

QPointF NodeItem::inputPortPos() const
{
    return mapToScene(QPointF(0, 30));
}

QPointF NodeItem::outputPortPos(const QString &port) const
{
    int idx = static_cast<int>(m_outputs.indexOf(port));
    if (idx < 0) {
        idx = 0;
    }
    return mapToScene(QPointF(m_width, 30 + idx * 18 + 9));
}

QString NodeItem::portAt(const QPointF &scenePos) const
{
    for (const QString &p : m_outputs) {
        if (QLineF(scenePos, outputPortPos(p)).length() < 10) {
            return p;
        }
    }
    return QString();
}

void NodeItem::setIssue(const QString &text)
{
    m_issue = text;
    setToolTip(text);
    update();
}

void NodeItem::setActive(bool active)
{
    m_active = active;
    update();
}

void NodeItem::paint(QPainter *p, const QStyleOptionGraphicsItem *, QWidget *)
{
    p->setRenderHint(QPainter::Antialiasing);
    const NodeSpec spec = NodeCatalog::spec(m_node.type);
    const QColor cat = categoryColor(spec.category);
    const QRectF body(0, 0, m_width, m_height);
    QColor bg = theme::panel();
    if (m_node.disabled) {
        bg = bg.darker(120);
    }
    p->setBrush(bg);
    QPen pen(isSelected() ? theme::accent() : (m_active ? theme::success() : (!m_issue.isEmpty() ? theme::danger() : theme::border())), isSelected() || m_active ? 2.5 : 1.2);
    p->setPen(pen);
    p->drawRoundedRect(body, 8, 8);
    // header stripe
    p->setPen(Qt::NoPen);
    p->setBrush(cat);
    QPainterPath header;
    header.addRoundedRect(QRectF(0, 0, m_width, 22), 8, 8);
    header.addRect(QRectF(0, 12, m_width, 10));
    p->drawPath(header.simplified());
    QFont f = p->font();
    f.setBold(true);
    f.setPointSizeF(8.5);
    p->setFont(f);
    p->setPen(Qt::white);
    p->drawText(QRectF(8, 0, m_width - 16, 22), Qt::AlignVCenter | Qt::AlignLeft, m_title + (m_node.disabled ? QStringLiteral(" (off)") : QString()));
    if (m_node.retryCount > 0) {
        p->drawText(QRectF(8, 0, m_width - 12, 22), Qt::AlignVCenter | Qt::AlignRight, QStringLiteral("↻%1").arg(m_node.retryCount));
    }
    f.setBold(false);
    f.setPointSizeF(7.5);
    p->setFont(f);
    p->setPen(theme::textMuted());
    p->drawText(QRectF(8, 26, m_width - 16, 18), Qt::AlignVCenter | Qt::AlignLeft, m_subtitle.isEmpty() ? spec.category : m_subtitle);
    // ports
    if (hasInput()) {
        p->setBrush(theme::text());
        p->setPen(QPen(theme::background(), 1));
        p->drawEllipse(QPointF(0, 30), 5, 5);
    }
    for (int i = 0; i < m_outputs.size(); ++i) {
        const QPointF c(m_width, 30 + i * 18 + 9);
        QColor pc = theme::text();
        const QString &o = m_outputs.at(i);
        if (o == QLatin1String("true") || o == QLatin1String("found") || o == QLatin1String("body")) {
            pc = theme::success();
        } else if (o == QLatin1String("false") || o == QLatin1String("timeout")) {
            pc = theme::danger();
        } else if (o == QLatin1String("done")) {
            pc = theme::warning();
        }
        p->setBrush(pc);
        p->setPen(QPen(theme::background(), 1));
        p->drawEllipse(c, 5, 5);
        if (m_outputs.size() > 1 || o != QLatin1String("out")) {
            p->setPen(theme::textMuted());
            p->drawText(QRectF(m_width - 70, c.y() - 8, 62, 16), Qt::AlignVCenter | Qt::AlignRight, o);
        }
    }
    if (!m_issue.isEmpty()) {
        p->setPen(theme::danger());
        p->drawText(QRectF(m_width - 22, m_height - 18, 18, 16), Qt::AlignCenter, QString(QChar(0x26A0)));
    }
}

QVariant NodeItem::itemChange(GraphicsItemChange change, const QVariant &value)
{
    if (change == ItemPositionHasChanged && m_scene) {
        m_scene->moveNode(m_node.id, pos(), false);
        m_scene->updateEdges();
    }
    if (change == ItemSelectedHasChanged && m_scene && value.toBool()) {
        emit m_scene->selectionNodeChanged(m_node.id);
    }
    return QGraphicsObject::itemChange(change, value);
}

void NodeItem::mousePressEvent(QGraphicsSceneMouseEvent *event)
{
    const QString port = portAt(event->scenePos());
    if (!port.isEmpty() && event->button() == Qt::LeftButton) {
        m_scene->beginConnection(this, port);
        event->accept();
        return;
    }
    if (event->button() == Qt::LeftButton) {
        m_scene->noteDragStart();
    }
    QGraphicsObject::mousePressEvent(event);
}

void NodeItem::mouseDoubleClickEvent(QGraphicsSceneMouseEvent *event)
{
    emit m_scene->nodeActivated(m_node.id);
    QGraphicsObject::mouseDoubleClickEvent(event);
}

// ================================================================= EdgeItem

EdgeItem::EdgeItem(const WorkflowConnection &connection, WorkflowScene *scene)
    : m_connection(connection)
    , m_scene(scene)
{
    setFlag(ItemIsSelectable);
    setZValue(0);
    QColor c = theme::textMuted();
    if (connection.port == QLatin1String("true") || connection.port == QLatin1String("found") || connection.port == QLatin1String("body")) {
        c = theme::success();
    } else if (connection.port == QLatin1String("false") || connection.port == QLatin1String("timeout")) {
        c = theme::danger();
    } else if (connection.port == QLatin1String("done")) {
        c = theme::warning();
    }
    setPen(QPen(c, 2));
    updatePath();
}

void EdgeItem::updatePath()
{
    NodeItem *from = m_scene->nodeItem(m_connection.from);
    NodeItem *to = m_scene->nodeItem(m_connection.to);
    if (!from || !to) {
        return;
    }
    const QPointF a = from->outputPortPos(m_connection.port);
    const QPointF b = to->inputPortPos();
    QPainterPath path(a);
    const qreal dx = std::max<qreal>(40, qAbs(b.x() - a.x()) / 2);
    path.cubicTo(a + QPointF(dx, 0), b - QPointF(dx, 0), b);
    setPath(path);
    QPen p = pen();
    p.setWidthF(isSelected() ? 3.5 : 2);
    setPen(p);
}

// ================================================================= WorkflowScene

WorkflowScene::WorkflowScene(QObject *parent)
    : QGraphicsScene(parent)
{
    setSceneRect(-2000, -2000, 8000, 6000);
    connect(this, &QGraphicsScene::selectionChanged, this, [this]() {
        for (EdgeItem *e : m_edges) {
            e->updatePath();
        }
    });
}

void WorkflowScene::setWorkflow(const Workflow &workflow)
{
    m_workflow = workflow;
    m_undo.clear();
    m_redo.clear();
    m_dirty = false;
    rebuild();
}

void WorkflowScene::rebuild()
{
    // clear() emits selectionChanged for every selected item it deletes; the handler
    // walks m_edges/m_nodes, so those must be empty and signals blocked before the
    // items go away. The selection is restored afterwards so the property panel keeps
    // its node.
    const QStringList selected = selectedNodeIds();
    {
        QSignalBlocker blocker(this);
        m_nodes.clear();
        m_edges.clear();
        m_tempEdge = nullptr;
        m_connectFrom = nullptr;
        clear();
    }
    for (const WorkflowNode &n : m_workflow.nodes) {
        auto *item = new NodeItem(n, this);
        addItem(item);
        m_nodes.insert(n.id, item);
    }
    for (const WorkflowConnection &c : m_workflow.connections) {
        if (m_nodes.contains(c.from) && m_nodes.contains(c.to)) {
            auto *e = new EdgeItem(c, this);
            addItem(e);
            m_edges.append(e);
        }
    }
    QString restored;
    {
        QSignalBlocker blocker(this);
        for (const QString &id : selected) {
            if (NodeItem *it = m_nodes.value(id, nullptr)) {
                it->setSelected(true);
                if (restored.isEmpty()) {
                    restored = id;
                }
            }
        }
    }
    updateEdges();
    if (!restored.isEmpty()) {
        emit selectionNodeChanged(restored);    // refresh the property panel (undo may have changed values)
    } else if (!selected.isEmpty()) {
        emit selectionChanged();                // the selected node is gone
    }
    if (!m_activeNode.isEmpty()) {
        setActiveNode(m_activeNode);
    }
}

void WorkflowScene::updateEdges()
{
    for (EdgeItem *e : m_edges) {
        e->updatePath();
    }
}

void WorkflowScene::pushUndo()
{
    m_undo.append(m_workflow.toJsonText());
    while (m_undo.size() > 100) {
        m_undo.removeFirst();
    }
    m_redo.clear();
}

void WorkflowScene::touched()
{
    m_dirty = true;
    emit modified();
}

QString WorkflowScene::addNode(const QString &type, const QPointF &pos)
{
    pushUndo();
    const QString id = m_workflow.addNode(type, pos);
    rebuild();
    if (NodeItem *item = m_nodes.value(id, nullptr)) {
        clearSelection();
        item->setSelected(true);
    }
    touched();
    return id;
}

QStringList WorkflowScene::selectedNodeIds() const
{
    QStringList ids;
    for (QGraphicsItem *it : selectedItems()) {
        if (auto *n = dynamic_cast<NodeItem *>(it)) {
            ids << n->nodeId();
        }
    }
    return ids;
}

void WorkflowScene::removeSelected()
{
    const QStringList ids = selectedNodeIds();
    QList<WorkflowConnection> edges;
    for (QGraphicsItem *it : selectedItems()) {
        if (auto *e = dynamic_cast<EdgeItem *>(it)) {
            edges << e->connection();
        }
    }
    if (ids.isEmpty() && edges.isEmpty()) {
        return;
    }
    pushUndo();
    for (const QString &id : ids) {
        if (m_workflow.node(id).type == QLatin1String("flow.start")) {
            continue;    // keep the entry point
        }
        m_workflow.removeNode(id);
    }
    for (const WorkflowConnection &c : edges) {
        m_workflow.disconnect(c.from, c.port, c.to);
    }
    rebuild();
    touched();
}

void WorkflowScene::moveNode(const QString &id, const QPointF &pos, bool pushUndoFlag)
{
    WorkflowNode *n = m_workflow.findNode(id);
    if (!n || n->pos == pos) {
        return;
    }
    if (pushUndoFlag) {
        pushUndo();
    }
    n->pos = pos;
    m_dirty = true;
}

void WorkflowScene::connectNodes(const QString &from, const QString &port, const QString &to)
{
    if (from == to || !m_workflow.hasNode(from) || !m_workflow.hasNode(to)) {
        return;
    }
    pushUndo();
    m_workflow.connectNodes(from, port, to);
    rebuild();
    touched();
}

void WorkflowScene::updateNode(const WorkflowNode &node)
{
    WorkflowNode *n = m_workflow.findNode(node.id);
    if (!n) {
        return;
    }
    pushUndo();
    *n = node;
    NodeItem *item = m_nodes.value(node.id, nullptr);
    if (item) {
        item->setNode(node);
    }
    if (node.type == QLatin1String("logic.switch") && item) {
        // Cases changed: drop edges leaving through ports that no longer exist. This
        // runs inside the property editor's own signal, so no scene rebuild here (it
        // would delete the very editor that is emitting).
        const QStringList ports = item->outputs();
        QList<EdgeItem *> stale;
        for (EdgeItem *e : std::as_const(m_edges)) {
            if (e->connection().from == node.id && !ports.contains(e->connection().port)) {
                stale << e;
            }
        }
        for (EdgeItem *e : stale) {
            m_edges.removeOne(e);
            removeItem(e);
            delete e;
        }
        m_workflow.connections.removeIf([&](const WorkflowConnection &c) { return c.from == node.id && !ports.contains(c.port); });
    }
    updateEdges();
    touched();
}

QString WorkflowScene::copySelectedJson() const
{
    const QStringList ids = selectedNodeIds();
    QJsonArray nodes;
    QJsonArray conns;
    for (const QString &id : ids) {
        nodes.append(m_workflow.node(id).toJson());
    }
    for (const WorkflowConnection &c : m_workflow.connections) {
        if (ids.contains(c.from) && ids.contains(c.to)) {
            conns.append(c.toJson());
        }
    }
    QJsonObject o;
    o[QStringLiteral("nodes")] = nodes;
    o[QStringLiteral("connections")] = conns;
    return QString::fromUtf8(QJsonDocument(o).toJson(QJsonDocument::Compact));
}

void WorkflowScene::pasteJson(const QString &json, const QPointF &offset)
{
    const QJsonObject o = QJsonDocument::fromJson(json.toUtf8()).object();
    const QJsonArray nodes = o.value(QStringLiteral("nodes")).toArray();
    if (nodes.isEmpty()) {
        return;
    }
    pushUndo();
    QHash<QString, QString> idMap;
    for (const QJsonValue &v : nodes) {
        WorkflowNode n = WorkflowNode::fromJson(v.toObject());
        if (n.type == QLatin1String("flow.start")) {
            continue;
        }
        const QString newId = Workflow::newId();
        idMap.insert(n.id, newId);
        n.id = newId;
        n.pos += offset;
        m_workflow.nodes.append(n);
    }
    for (const QJsonValue &v : o.value(QStringLiteral("connections")).toArray()) {
        WorkflowConnection c = WorkflowConnection::fromJson(v.toObject());
        if (idMap.contains(c.from) && idMap.contains(c.to)) {
            c.from = idMap.value(c.from);
            c.to = idMap.value(c.to);
            m_workflow.connections.append(c);
        }
    }
    rebuild();
    clearSelection();
    for (const QString &id : idMap.values()) {
        if (NodeItem *it = m_nodes.value(id, nullptr)) {
            it->setSelected(true);
        }
    }
    touched();
}

void WorkflowScene::duplicateSelected()
{
    pasteJson(copySelectedJson(), QPointF(40, 40));
}

void WorkflowScene::undo()
{
    if (m_undo.isEmpty()) {
        return;
    }
    m_redo.append(m_workflow.toJsonText());
    m_workflow = Workflow::fromJsonText(m_undo.takeLast());
    rebuild();
    touched();
}

void WorkflowScene::redo()
{
    if (m_redo.isEmpty()) {
        return;
    }
    m_undo.append(m_workflow.toJsonText());
    m_workflow = Workflow::fromJsonText(m_redo.takeLast());
    rebuild();
    touched();
}

void WorkflowScene::beginConnection(NodeItem *from, const QString &port)
{
    m_connectFrom = from;
    m_connectPort = port;
    if (!m_tempEdge) {
        m_tempEdge = addPath(QPainterPath(), QPen(theme::accent(), 2, Qt::DashLine));
        m_tempEdge->setZValue(2);
    }
}

void WorkflowScene::mouseMoveEvent(QGraphicsSceneMouseEvent *event)
{
    if (m_connectFrom && m_tempEdge) {
        const QPointF a = m_connectFrom->outputPortPos(m_connectPort);
        const QPointF b = event->scenePos();
        QPainterPath path(a);
        const qreal dx = std::max<qreal>(40, qAbs(b.x() - a.x()) / 2);
        path.cubicTo(a + QPointF(dx, 0), b - QPointF(dx, 0), b);
        m_tempEdge->setPath(path);
        return;
    }
    QGraphicsScene::mouseMoveEvent(event);
}

void WorkflowScene::mouseReleaseEvent(QGraphicsSceneMouseEvent *event)
{
    if (m_connectFrom) {
        NodeItem *target = nullptr;
        for (QGraphicsItem *it : items(event->scenePos())) {
            if (auto *n = dynamic_cast<NodeItem *>(it)) {
                target = n;
                break;
            }
        }
        const QString from = m_connectFrom->nodeId();
        const QString port = m_connectPort;
        m_connectFrom = nullptr;
        if (m_tempEdge) {
            removeItem(m_tempEdge);
            delete m_tempEdge;
            m_tempEdge = nullptr;
        }
        if (target && target->hasInput() && target->nodeId() != from) {
            connectNodes(from, port, target->nodeId());
        }
        // The source node accepted the press and is still the scene's mouse grabber.
        if (QGraphicsItem *grabber = mouseGrabberItem()) {
            grabber->ungrabMouse();
        }
        return;
    }
    QGraphicsScene::mouseReleaseEvent(event);
    // A finished drag of nodes: one undo step (the pre-drag model) for the whole move.
    if (!m_dragSnapshot.isEmpty()) {
        bool moved = false;
        for (auto it = m_nodes.cbegin(); it != m_nodes.cend(); ++it) {
            if (m_dragStartPos.contains(it.key()) && m_dragStartPos.value(it.key()) != it.value()->pos()) {
                moved = true;
                break;
            }
        }
        if (moved) {
            m_undo.append(m_dragSnapshot);
            while (m_undo.size() > 100) {
                m_undo.removeFirst();
            }
            m_redo.clear();
            touched();
        }
        m_dragSnapshot.clear();
        m_dragStartPos.clear();
    }
}

void WorkflowScene::noteDragStart()
{
    m_dragSnapshot = m_workflow.toJsonText();
    m_dragStartPos.clear();
    for (auto it = m_nodes.cbegin(); it != m_nodes.cend(); ++it) {
        m_dragStartPos.insert(it.key(), it.value()->pos());
    }
}

void WorkflowScene::drawBackground(QPainter *painter, const QRectF &rect)
{
    painter->fillRect(rect, theme::background());
    painter->setPen(QPen(theme::border().darker(110), 0.5));
    const int grid = 24;
    const int left = static_cast<int>(rect.left()) - (static_cast<int>(rect.left()) % grid);
    const int top = static_cast<int>(rect.top()) - (static_cast<int>(rect.top()) % grid);
    for (int x = left; x < rect.right(); x += grid) {
        for (int y = top; y < rect.bottom(); y += grid) {
            painter->drawPoint(x, y);
        }
    }
}

void WorkflowScene::setActiveNode(const QString &id)
{
    if (NodeItem *old = m_nodes.value(m_activeNode, nullptr)) {
        old->setActive(false);
    }
    m_activeNode = id;
    if (NodeItem *n = m_nodes.value(id, nullptr)) {
        n->setActive(true);
    }
}

// ================================================================= WorkflowView

WorkflowView::WorkflowView(QWidget *parent)
    : QGraphicsView(parent)
{
    setRenderHint(QPainter::Antialiasing);
    setDragMode(QGraphicsView::RubberBandDrag);
    setViewportUpdateMode(QGraphicsView::BoundingRectViewportUpdate);
    setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
    setAcceptDrops(true);
    setFrameShape(QFrame::NoFrame);
}

void WorkflowView::fitAll()
{
    if (!scene() || scene()->items().isEmpty()) {
        return;
    }
    fitInView(scene()->itemsBoundingRect().adjusted(-40, -40, 40, 40), Qt::KeepAspectRatio);
    if (transform().m11() > 1.2) {
        resetTransform();
        centerOn(scene()->itemsBoundingRect().center());
    }
}

void WorkflowView::zoomIn()
{
    scale(1.2, 1.2);
}

void WorkflowView::zoomOut()
{
    scale(1 / 1.2, 1 / 1.2);
}

void WorkflowView::resetZoom()
{
    resetTransform();
}

void WorkflowView::wheelEvent(QWheelEvent *event)
{
    if (event->modifiers() & Qt::ControlModifier) {
        if (event->angleDelta().y() > 0) {
            zoomIn();
        } else {
            zoomOut();
        }
        event->accept();
        return;
    }
    QGraphicsView::wheelEvent(event);
}

void WorkflowView::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::MiddleButton || (event->button() == Qt::LeftButton && (event->modifiers() & Qt::AltModifier))) {
        m_panning = true;
        m_panStart = event->pos();
        setCursor(Qt::ClosedHandCursor);
        event->accept();
        return;
    }
    QGraphicsView::mousePressEvent(event);
}

void WorkflowView::mouseMoveEvent(QMouseEvent *event)
{
    if (m_panning) {
        const QPoint d = event->pos() - m_panStart;
        m_panStart = event->pos();
        horizontalScrollBar()->setValue(horizontalScrollBar()->value() - d.x());
        verticalScrollBar()->setValue(verticalScrollBar()->value() - d.y());
        return;
    }
    QGraphicsView::mouseMoveEvent(event);
}

void WorkflowView::mouseReleaseEvent(QMouseEvent *event)
{
    if (m_panning) {
        m_panning = false;
        setCursor(Qt::ArrowCursor);
        return;
    }
    QGraphicsView::mouseReleaseEvent(event);
}

void WorkflowView::dragEnterEvent(QDragEnterEvent *event)
{
    if (event->mimeData()->hasFormat(QLatin1String(kNodeMime))) {
        event->acceptProposedAction();
    }
}

void WorkflowView::dragMoveEvent(QDragMoveEvent *event)
{
    if (event->mimeData()->hasFormat(QLatin1String(kNodeMime))) {
        event->acceptProposedAction();
    }
}

void WorkflowView::dropEvent(QDropEvent *event)
{
    if (event->mimeData()->hasFormat(QLatin1String(kNodeMime))) {
        emit dropNode(QString::fromUtf8(event->mimeData()->data(QLatin1String(kNodeMime))), mapToScene(event->position().toPoint()));
        event->acceptProposedAction();
    }
}

// ================================================================= WorkflowEditor

WorkflowEditor::WorkflowEditor(QWidget *parent)
    : QWidget(parent)
{
    m_scene = new WorkflowScene(this);
    m_view = new WorkflowView(this);
    m_view->setScene(m_scene);

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(4);
    auto *body = new QHBoxLayout();
    body->setSpacing(6);

    m_palette = new QTreeWidget(this);
    m_palette->setHeaderHidden(true);
    m_palette->setFixedWidth(190);
    m_palette->setDragEnabled(true);
    m_palette->setToolTip(tr("Double-click or drag a node onto the canvas"));
    buildPalette();
    body->addWidget(m_palette);
    body->addWidget(m_view, 1);

    m_propsScroll = new QScrollArea(this);
    m_propsScroll->setWidgetResizable(true);
    m_propsScroll->setFixedWidth(300);
    m_propsScroll->setFrameShape(QFrame::NoFrame);
    m_props = new QWidget(m_propsScroll);
    m_props->setObjectName(QStringLiteral("sidePanel"));
    auto *pv = new QVBoxLayout(m_props);
    pv->setContentsMargins(10, 8, 10, 8);
    m_propsTitle = new QLabel(tr("Select a node"), m_props);
    m_propsTitle->setStyleSheet(QStringLiteral("font-weight:bold; font-size:13px;"));
    m_propsTitle->setWordWrap(true);
    pv->addWidget(m_propsTitle);
    m_propsForm = new QFormLayout();
    m_propsForm->setLabelAlignment(Qt::AlignLeft);
    m_propsForm->setRowWrapPolicy(QFormLayout::WrapAllRows);
    pv->addLayout(m_propsForm);
    pv->addStretch(1);
    m_propsScroll->setWidget(m_props);
    body->addWidget(m_propsScroll);
    root->addLayout(body, 1);

    m_issues = new QListWidget(this);
    m_issues->setFixedHeight(70);
    root->addWidget(m_issues);

    connect(m_scene, &WorkflowScene::selectionNodeChanged, this, &WorkflowEditor::showProperties);
    connect(m_scene, &WorkflowScene::modified, this, [this]() {
        emit modified();
        validate();
    });
    connect(m_scene, &QGraphicsScene::selectionChanged, this, [this]() {
        const QStringList ids = m_scene->selectedNodeIds();
        if (ids.isEmpty()) {
            showProperties(QString());
        }
    });
    connect(m_view, &WorkflowView::dropNode, this, [this](const QString &type, const QPointF &pos) { m_scene->addNode(type, pos); });
    connect(m_palette, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem *item, int) {
        const QString type = item->data(0, Qt::UserRole).toString();
        if (!type.isEmpty()) {
            m_scene->addNode(type, m_view->mapToScene(m_view->viewport()->rect().center()) + QPointF(20, 20));
        }
    });
    connect(m_issues, &QListWidget::itemClicked, this, [this](QListWidgetItem *it) {
        const QString id = it->data(Qt::UserRole).toString();
        if (NodeItem *n = m_scene->nodeItem(id)) {
            m_scene->clearSelection();
            n->setSelected(true);
            m_view->centerOn(n);
        }
    });

    new QShortcut(QKeySequence::Delete, m_view, [this]() { m_scene->removeSelected(); });
    new QShortcut(QKeySequence::Undo, this, [this]() { m_scene->undo(); });
    new QShortcut(QKeySequence::Redo, this, [this]() { m_scene->redo(); });
    new QShortcut(QKeySequence::Copy, m_view, [this]() { QApplication::clipboard()->setText(m_scene->copySelectedJson()); });
    new QShortcut(QKeySequence::Paste, m_view, [this]() { m_scene->pasteJson(QApplication::clipboard()->text(), QPointF(40, 40)); });
    new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_D), m_view, [this]() { m_scene->duplicateSelected(); });
    new QShortcut(QKeySequence::SelectAll, m_view, [this]() {
        for (QGraphicsItem *it : m_scene->items()) {
            it->setSelected(true);
        }
    });
    new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_0), m_view, [this]() { m_view->fitAll(); });
}

void WorkflowEditor::buildPalette()
{
    m_palette->clear();
    for (const QString &cat : NodeCatalog::categories()) {
        auto *parent = new QTreeWidgetItem(m_palette, { cat });
        parent->setFlags(Qt::ItemIsEnabled);
        parent->setExpanded(cat == QLatin1String("Interaction") || cat == QLatin1String("Flow"));
        for (const NodeSpec &spec : NodeCatalog::inCategory(cat)) {
            auto *item = new QTreeWidgetItem(parent, { spec.title });
            item->setData(0, Qt::UserRole, spec.type);
            item->setToolTip(0, spec.help.isEmpty() ? spec.type : spec.help);
            item->setForeground(0, categoryColor(cat).lighter(130));
        }
    }
    // Drag support: QTreeWidget's default mime is not what the view expects, so
    // start the drag manually.
    m_palette->viewport()->installEventFilter(this);
}

// Palette drag start
bool paletteDrag(QTreeWidget *tree, QEvent *event)
{
    if (event->type() == QEvent::MouseMove) {
        auto *me = static_cast<QMouseEvent *>(event);
        if (me->buttons() & Qt::LeftButton) {
            QTreeWidgetItem *item = tree->currentItem();
            if (item && !item->data(0, Qt::UserRole).toString().isEmpty()) {
                auto *drag = new QDrag(tree);
                auto *mime = new QMimeData;
                mime->setData(QLatin1String(kNodeMime), item->data(0, Qt::UserRole).toString().toUtf8());
                drag->setMimeData(mime);
                drag->exec(Qt::CopyAction);
                return true;
            }
        }
    }
    return false;
}

void WorkflowEditor::setWorkflow(const Workflow &workflow)
{
    m_scene->setWorkflow(workflow);
    showProperties(QString());
    validate();
    m_view->fitAll();
}

void WorkflowEditor::validate()
{
    m_issues->clear();
    for (NodeItem *n : m_scene->nodeItems()) {
        n->setIssue(QString());
    }
    const QList<ValidationIssue> issues = WorkflowValidator::validate(m_scene->workflow());
    int errors = 0;
    int warnings = 0;
    for (const ValidationIssue &i : issues) {
        auto *item = new QListWidgetItem((i.error ? QStringLiteral("✖ ") : QStringLiteral("⚠ ")) + i.message, m_issues);
        item->setForeground(i.error ? theme::danger() : theme::warning());
        item->setData(Qt::UserRole, i.nodeId);
        if (NodeItem *n = m_scene->nodeItem(i.nodeId)) {
            n->setIssue(i.message);
        }
        (i.error ? errors : warnings)++;
    }
    if (issues.isEmpty()) {
        auto *ok = new QListWidgetItem(tr("✔ Workflow is valid"), m_issues);
        ok->setForeground(theme::success());
    }
    emit issuesChanged(errors, warnings);
}

QWidget *WorkflowEditor::editorFor(const ParamSpec &spec, const QVariant &value)
{
    const QString t = spec.type;
    if (t == QLatin1String("bool")) {
        auto *c = new QCheckBox(m_props);
        c->setChecked(value.toBool());
        connect(c, &QCheckBox::toggled, this, [this](bool) { applyProperty(); });
        return c;
    }
    if (t == QLatin1String("int") || t == QLatin1String("duration")) {
        auto *s = new QSpinBox(m_props);
        s->setRange(-1000000, 100000000);
        s->setValue(value.toInt());
        if (t == QLatin1String("duration")) {
            s->setSuffix(QStringLiteral(" ms"));
        }
        connect(s, &QSpinBox::valueChanged, this, [this](int) { applyProperty(); });
        return s;
    }
    if (t == QLatin1String("double")) {
        auto *s = new QDoubleSpinBox(m_props);
        s->setRange(-100000, 100000);
        s->setDecimals(3);
        s->setSingleStep(0.05);
        s->setValue(value.toDouble());
        connect(s, &QDoubleSpinBox::valueChanged, this, [this](double) { applyProperty(); });
        return s;
    }
    if (t == QLatin1String("enum")) {
        auto *c = new QComboBox(m_props);
        c->addItems(spec.options);
        c->setCurrentText(value.toString());
        connect(c, &QComboBox::currentIndexChanged, this, [this](int) { applyProperty(); });
        return c;
    }
    if (t == QLatin1String("text")) {
        auto *e = new QPlainTextEdit(m_props);
        e->setPlainText(value.toString());
        e->setFixedHeight(70);
        connect(e, &QPlainTextEdit::textChanged, this, [this]() { applyProperty(); });
        return e;
    }
    if (t == QLatin1String("file") || t == QLatin1String("image")) {
        auto *w = new QWidget(m_props);
        auto *h = new QHBoxLayout(w);
        h->setContentsMargins(0, 0, 0, 0);
        auto *e = new QLineEdit(value.toString(), w);
        e->setObjectName(QStringLiteral("value"));
        auto *b = theme::button(QStringLiteral("…"), w);
        b->setFixedWidth(28);
        h->addWidget(e, 1);
        h->addWidget(b);
        connect(e, &QLineEdit::editingFinished, this, [this]() { applyProperty(); });
        connect(b, &QPushButton::clicked, this, [this, e, t]() {
            const QString f = QFileDialog::getOpenFileName(this, tr("Choose file"), e->text(), t == QLatin1String("image") ? tr("Images (*.png *.jpg *.jpeg *.bmp)") : tr("All files (*.*)"));
            if (!f.isEmpty()) {
                e->setText(f);
                applyProperty();
            }
        });
        return w;
    }
    auto *e = new QLineEdit(value.toString(), m_props);
    e->setPlaceholderText(spec.help);
    connect(e, &QLineEdit::editingFinished, this, [this]() { applyProperty(); });
    connect(e, &QLineEdit::textEdited, this, [this](const QString &) { applyProperty(); });
    return e;
}

void WorkflowEditor::showProperties(const QString &nodeId)
{
    m_loadingProps = true;
    while (m_propsForm->rowCount() > 0) {
        m_propsForm->removeRow(0);
    }
    m_paramWidgets.clear();
    m_currentNode = nodeId;
    if (nodeId.isEmpty() || !m_scene->workflow().hasNode(nodeId)) {
        m_propsTitle->setText(tr("Select a node to edit its parameters.\n\nTips: drag from an output port to another node to connect · Ctrl+wheel zooms · middle-drag pans · Del removes · Ctrl+Z/Y undo/redo · Ctrl+C/V copy/paste · Ctrl+D duplicate."));
        m_loadingProps = false;
        return;
    }
    const WorkflowNode node = m_scene->workflow().node(nodeId);
    const NodeSpec spec = NodeCatalog::spec(node.type);
    m_propsTitle->setText(spec.title + (spec.help.isEmpty() ? QString() : QStringLiteral("\n") + spec.help));
    auto *title = new QLineEdit(node.title, m_props);
    title->setPlaceholderText(spec.title);
    connect(title, &QLineEdit::textEdited, this, [this](const QString &) { applyProperty(); });
    m_paramWidgets.insert(QStringLiteral("__title"), title);
    m_propsForm->addRow(tr("Label"), title);
    for (const ParamSpec &p : spec.params) {
        QWidget *w = editorFor(p, node.params.value(p.key, p.defaultValue));
        m_paramWidgets.insert(p.key, w);
        m_propsForm->addRow(p.label + (p.required ? QStringLiteral(" *") : QString()), w);
    }
    m_propsForm->addRow(theme::sectionTitle(tr("Failure handling"), m_props));
    auto *retry = new QSpinBox(m_props);
    retry->setRange(0, 50);
    retry->setValue(node.retryCount);
    auto *delay = new QSpinBox(m_props);
    delay->setRange(0, 600000);
    delay->setSuffix(QStringLiteral(" ms"));
    delay->setValue(node.retryDelayMs);
    auto *timeout = new QSpinBox(m_props);
    timeout->setRange(0, 3600000);
    timeout->setSuffix(QStringLiteral(" ms"));
    timeout->setSpecialValueText(tr("default"));
    timeout->setValue(node.timeoutMs);
    auto *onFail = new QComboBox(m_props);
    onFail->addItem(tr("Fail this device (screenshot)"), QStringLiteral("fail"));
    onFail->addItem(tr("Continue to next node"), QStringLiteral("continue"));
    onFail->addItem(tr("Stop this device"), QStringLiteral("stop"));
    onFail->setCurrentIndex(std::max(0, onFail->findData(node.onFailure)));
    auto *disabled = new QCheckBox(tr("Disabled (skipped)"), m_props);
    disabled->setChecked(node.disabled);
    for (QSpinBox *s : { retry, delay, timeout }) {
        connect(s, &QSpinBox::valueChanged, this, [this](int) { applyProperty(); });
    }
    connect(onFail, &QComboBox::currentIndexChanged, this, [this](int) { applyProperty(); });
    connect(disabled, &QCheckBox::toggled, this, [this](bool) { applyProperty(); });
    m_paramWidgets.insert(QStringLiteral("__retry"), retry);
    m_paramWidgets.insert(QStringLiteral("__delay"), delay);
    m_paramWidgets.insert(QStringLiteral("__timeout"), timeout);
    m_paramWidgets.insert(QStringLiteral("__onFail"), onFail);
    m_paramWidgets.insert(QStringLiteral("__disabled"), disabled);
    m_propsForm->addRow(tr("Retries"), retry);
    m_propsForm->addRow(tr("Retry delay"), delay);
    m_propsForm->addRow(tr("Timeout"), timeout);
    m_propsForm->addRow(tr("On failure"), onFail);
    m_propsForm->addRow(QString(), disabled);
    m_loadingProps = false;
}

void WorkflowEditor::applyProperty()
{
    if (m_loadingProps || m_currentNode.isEmpty()) {
        return;
    }
    WorkflowNode node = m_scene->workflow().node(m_currentNode);
    if (node.id.isEmpty()) {
        return;
    }
    const NodeSpec spec = NodeCatalog::spec(node.type);
    for (const ParamSpec &p : spec.params) {
        QWidget *w = m_paramWidgets.value(p.key, nullptr);
        if (!w) {
            continue;
        }
        if (auto *c = qobject_cast<QCheckBox *>(w)) {
            node.params[p.key] = c->isChecked();
        } else if (auto *s = qobject_cast<QSpinBox *>(w)) {
            node.params[p.key] = s->value();
        } else if (auto *d = qobject_cast<QDoubleSpinBox *>(w)) {
            node.params[p.key] = d->value();
        } else if (auto *cb = qobject_cast<QComboBox *>(w)) {
            node.params[p.key] = cb->currentText();
        } else if (auto *t = qobject_cast<QPlainTextEdit *>(w)) {
            node.params[p.key] = t->toPlainText();
        } else if (auto *e = qobject_cast<QLineEdit *>(w)) {
            node.params[p.key] = e->text();
        } else if (auto *inner = w->findChild<QLineEdit *>(QStringLiteral("value"))) {
            node.params[p.key] = inner->text();
        }
    }
    node.title = qobject_cast<QLineEdit *>(m_paramWidgets.value(QStringLiteral("__title")))->text();
    node.retryCount = qobject_cast<QSpinBox *>(m_paramWidgets.value(QStringLiteral("__retry")))->value();
    node.retryDelayMs = qobject_cast<QSpinBox *>(m_paramWidgets.value(QStringLiteral("__delay")))->value();
    node.timeoutMs = qobject_cast<QSpinBox *>(m_paramWidgets.value(QStringLiteral("__timeout")))->value();
    node.onFailure = qobject_cast<QComboBox *>(m_paramWidgets.value(QStringLiteral("__onFail")))->currentData().toString();
    node.disabled = qobject_cast<QCheckBox *>(m_paramWidgets.value(QStringLiteral("__disabled")))->isChecked();
    m_scene->updateNode(node);
}

} // namespace farm

// The palette drag hook is installed through eventFilter on the tree viewport.
bool farm::WorkflowEditor::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_palette->viewport() && paletteDrag(m_palette, event)) {
        return true;
    }
    return QWidget::eventFilter(watched, event);
}
