#ifndef FARM_UI_WORKFLOWEDITOR_H
#define FARM_UI_WORKFLOWEDITOR_H

#include <QGraphicsObject>
#include <QGraphicsPathItem>
#include <QGraphicsScene>
#include <QGraphicsView>
#include <QHash>
#include <QList>
#include <QWidget>

#include "automation/nodecatalog.h"
#include "automation/workflowmodel.h"

class QTreeWidget;
class QFormLayout;
class QListWidget;
class QLabel;
class QScrollArea;

namespace farm {

class WorkflowScene;

/** One node on the canvas. */
class NodeItem : public QGraphicsObject
{
    Q_OBJECT
public:
    NodeItem(const WorkflowNode &node, WorkflowScene *scene);
    QRectF boundingRect() const override;
    void paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget) override;
    QString nodeId() const { return m_node.id; }
    const WorkflowNode &node() const { return m_node; }
    void setNode(const WorkflowNode &node);
    QPointF inputPortPos() const;                 // scene coords
    QPointF outputPortPos(const QString &port) const;
    QStringList outputs() const { return m_outputs; }
    QString portAt(const QPointF &scenePos) const;   // output port under the cursor, empty if none
    bool hasInput() const { return m_node.type != QLatin1String("flow.start"); }
    void setIssue(const QString &text);
    void setActive(bool active);

protected:
    QVariant itemChange(GraphicsItemChange change, const QVariant &value) override;
    void mousePressEvent(QGraphicsSceneMouseEvent *event) override;
    void mouseDoubleClickEvent(QGraphicsSceneMouseEvent *event) override;

private:
    WorkflowNode m_node;
    WorkflowScene *m_scene;
    QStringList m_outputs;
    QString m_title;
    QString m_subtitle;
    QString m_issue;
    bool m_active = false;
    qreal m_width = 190;
    qreal m_height = 64;
};

/** A connection (bezier). */
class EdgeItem : public QGraphicsPathItem
{
public:
    EdgeItem(const WorkflowConnection &connection, WorkflowScene *scene);
    void updatePath();
    const WorkflowConnection &connection() const { return m_connection; }

private:
    WorkflowConnection m_connection;
    WorkflowScene *m_scene;
};

/** Holds the model and the items; every mutation goes through here (undo-able). */
class WorkflowScene : public QGraphicsScene
{
    Q_OBJECT
public:
    explicit WorkflowScene(QObject *parent = nullptr);
    void setWorkflow(const Workflow &workflow);
    Workflow workflow() const { return m_workflow; }
    NodeItem *nodeItem(const QString &id) const { return m_nodes.value(id, nullptr); }
    void rebuild();
    void updateEdges();

    // mutations (push undo)
    QString addNode(const QString &type, const QPointF &pos);
    void removeSelected();
    void moveNode(const QString &id, const QPointF &pos, bool pushUndo);
    void connectNodes(const QString &from, const QString &port, const QString &to);
    void updateNode(const WorkflowNode &node);
    void duplicateSelected();
    QString copySelectedJson() const;
    void pasteJson(const QString &json, const QPointF &offset);
    void undo();
    void redo();
    bool canUndo() const { return !m_undo.isEmpty(); }
    bool canRedo() const { return !m_redo.isEmpty(); }
    void markClean() { m_dirty = false; }
    bool isDirty() const { return m_dirty; }
    void beginConnection(NodeItem *from, const QString &port);
    void setActiveNode(const QString &id);
    QStringList selectedNodeIds() const;

signals:
    void modified();
    void selectionNodeChanged(const QString &id);
    void nodeActivated(const QString &id);

protected:
    void mouseMoveEvent(QGraphicsSceneMouseEvent *event) override;
    void mouseReleaseEvent(QGraphicsSceneMouseEvent *event) override;
    void drawBackground(QPainter *painter, const QRectF &rect) override;

private:
    void pushUndo();
    void touched();

    Workflow m_workflow;
    QHash<QString, NodeItem *> m_nodes;
    QList<EdgeItem *> m_edges;
    QList<QString> m_undo;
    QList<QString> m_redo;
    bool m_dirty = false;
    NodeItem *m_connectFrom = nullptr;
    QString m_connectPort;
    QGraphicsPathItem *m_tempEdge = nullptr;
    QString m_activeNode;
};

/** Zoom / pan view. */
class WorkflowView : public QGraphicsView
{
    Q_OBJECT
public:
    explicit WorkflowView(QWidget *parent = nullptr);
    void fitAll();
    void zoomIn();
    void zoomOut();
    void resetZoom();

signals:
    void dropNode(const QString &type, const QPointF &scenePos);

protected:
    void wheelEvent(QWheelEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dragMoveEvent(QDragMoveEvent *event) override;
    void dropEvent(QDropEvent *event) override;

private:
    bool m_panning = false;
    QPoint m_panStart;
};

/**
 * The complete editor: palette | canvas | property panel, plus validation list.
 */
class WorkflowEditor : public QWidget
{
    Q_OBJECT
public:
    explicit WorkflowEditor(QWidget *parent = nullptr);
    void setWorkflow(const Workflow &workflow);
    Workflow workflow() const { return m_scene->workflow(); }
    bool isDirty() const { return m_scene->isDirty(); }
    void markClean() { m_scene->markClean(); }
    void validate();
    void fitAll() { m_view->fitAll(); }
    void highlightNode(const QString &id) { m_scene->setActiveNode(id); }
    WorkflowScene *scene() const { return m_scene; }

signals:
    void modified();
    void issuesChanged(int errors, int warnings);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void buildPalette();
    void showProperties(const QString &nodeId);
    void applyProperty();
    QWidget *editorFor(const ParamSpec &spec, const QVariant &value);

    WorkflowScene *m_scene = nullptr;
    WorkflowView *m_view = nullptr;
    QTreeWidget *m_palette = nullptr;
    QWidget *m_props = nullptr;
    QFormLayout *m_propsForm = nullptr;
    QScrollArea *m_propsScroll = nullptr;
    QLabel *m_propsTitle = nullptr;
    QListWidget *m_issues = nullptr;
    QString m_currentNode;
    QHash<QString, QWidget *> m_paramWidgets;
    bool m_loadingProps = false;
};

} // namespace farm

#endif // FARM_UI_WORKFLOWEDITOR_H
