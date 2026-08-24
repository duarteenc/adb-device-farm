#ifndef FARM_AUTOMATION_WORKFLOWMODEL_H
#define FARM_AUTOMATION_WORKFLOWMODEL_H

#include <QJsonObject>
#include <QList>
#include <QPointF>
#include <QString>
#include <QStringList>
#include <QVariantMap>

namespace farm {

/**
 * Serialisable workflow model — independent from the editor widgets and from
 * the engine. Positions are stored for the editor but nothing in the runtime
 * depends on them.
 *
 * {
 *   "name": "Example", "version": 1, "variables": {"count": 3},
 *   "nodes": [{"id":"n1","type":"flow.start","params":{},"pos":[0,0]}, ...],
 *   "connections": [{"from":"n1","port":"out","to":"n2"}, ...]
 * }
 */
struct WorkflowNode
{
    QString id;
    QString type;            // NodeCatalog type key, e.g. "input.tap"
    QString title;           // optional operator label
    QVariantMap params;
    QPointF pos;
    int retryCount = 0;
    int retryDelayMs = 1000;
    int timeoutMs = 0;       // 0 = node default
    QString onFailure = QStringLiteral("fail");    // fail | continue | stop
    bool disabled = false;

    QJsonObject toJson() const;
    static WorkflowNode fromJson(const QJsonObject &o);
};

struct WorkflowConnection
{
    QString from;
    QString port = QStringLiteral("out");    // out | true | false | body | done | found | timeout | case:<value> | default
    QString to;

    QJsonObject toJson() const;
    static WorkflowConnection fromJson(const QJsonObject &o);
};

struct Workflow
{
    QString id;
    QString name;
    QString description;
    int version = 1;
    QVariantMap variables;
    QList<WorkflowNode> nodes;
    QList<WorkflowConnection> connections;
    QVariantMap runDefaults;    // targetsMode (selection|group|all), group, concurrency

    QJsonObject toJson() const;
    QString toJsonText() const;
    static Workflow fromJson(const QJsonObject &o, QString *error = nullptr);
    static Workflow fromJsonText(const QString &text, QString *error = nullptr);

    bool hasNode(const QString &id) const;
    WorkflowNode node(const QString &id) const;
    WorkflowNode *findNode(const QString &id);
    QString startNodeId() const;
    QStringList nextNodes(const QString &nodeId, const QString &port) const;
    QString nextNode(const QString &nodeId, const QString &port) const;    // first or empty
    QString addNode(const QString &type, const QPointF &pos, const QVariantMap &params = QVariantMap());
    void removeNode(const QString &id);
    void connectNodes(const QString &from, const QString &port, const QString &to);
    void disconnect(const QString &from, const QString &port, const QString &to);
    static QString newId();
    static Workflow makeEmpty(const QString &name);
};

struct ValidationIssue
{
    QString nodeId;
    QString message;
    bool error = true;
};

class WorkflowValidator
{
public:
    static QList<ValidationIssue> validate(const Workflow &workflow);
    static bool isValid(const Workflow &workflow, QString *firstError = nullptr);
};

} // namespace farm

#endif // FARM_AUTOMATION_WORKFLOWMODEL_H
