#include "workflowmodel.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QSet>
#include <QUuid>

#include "nodecatalog.h"

namespace farm {

// ---------------------------------------------------------------- node

QJsonObject WorkflowNode::toJson() const
{
    QJsonObject o;
    o[QStringLiteral("id")] = id;
    o[QStringLiteral("type")] = type;
    if (!title.isEmpty()) {
        o[QStringLiteral("title")] = title;
    }
    o[QStringLiteral("params")] = QJsonObject::fromVariantMap(params);
    o[QStringLiteral("pos")] = QJsonArray{ pos.x(), pos.y() };
    if (retryCount > 0) {
        o[QStringLiteral("retryCount")] = retryCount;
        o[QStringLiteral("retryDelayMs")] = retryDelayMs;
    }
    if (timeoutMs > 0) {
        o[QStringLiteral("timeoutMs")] = timeoutMs;
    }
    if (onFailure != QLatin1String("fail")) {
        o[QStringLiteral("onFailure")] = onFailure;
    }
    if (disabled) {
        o[QStringLiteral("disabled")] = true;
    }
    return o;
}

WorkflowNode WorkflowNode::fromJson(const QJsonObject &o)
{
    WorkflowNode n;
    n.id = o.value(QStringLiteral("id")).toString();
    n.type = o.value(QStringLiteral("type")).toString();
    n.title = o.value(QStringLiteral("title")).toString();
    n.params = o.value(QStringLiteral("params")).toObject().toVariantMap();
    const QJsonArray pos = o.value(QStringLiteral("pos")).toArray();
    if (pos.size() == 2) {
        n.pos = QPointF(pos.at(0).toDouble(), pos.at(1).toDouble());
    }
    n.retryCount = o.value(QStringLiteral("retryCount")).toInt();
    n.retryDelayMs = o.value(QStringLiteral("retryDelayMs")).toInt(1000);
    n.timeoutMs = o.value(QStringLiteral("timeoutMs")).toInt();
    n.onFailure = o.value(QStringLiteral("onFailure")).toString(QStringLiteral("fail"));
    n.disabled = o.value(QStringLiteral("disabled")).toBool();
    return n;
}

// ---------------------------------------------------------------- connection

QJsonObject WorkflowConnection::toJson() const
{
    QJsonObject o;
    o[QStringLiteral("from")] = from;
    o[QStringLiteral("port")] = port;
    o[QStringLiteral("to")] = to;
    return o;
}

WorkflowConnection WorkflowConnection::fromJson(const QJsonObject &o)
{
    WorkflowConnection c;
    c.from = o.value(QStringLiteral("from")).toString();
    c.port = o.value(QStringLiteral("port")).toString(QStringLiteral("out"));
    c.to = o.value(QStringLiteral("to")).toString();
    return c;
}

// ---------------------------------------------------------------- workflow

QString Workflow::newId()
{
    return QUuid::createUuid().toString(QUuid::WithoutBraces).left(8);
}

Workflow Workflow::makeEmpty(const QString &name)
{
    Workflow w;
    w.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    w.name = name;
    const QString start = w.addNode(QStringLiteral("flow.start"), QPointF(40, 120));
    const QString end = w.addNode(QStringLiteral("flow.end"), QPointF(420, 120));
    w.connectNodes(start, QStringLiteral("out"), end);
    return w;
}

QJsonObject Workflow::toJson() const
{
    QJsonObject o;
    o[QStringLiteral("id")] = id;
    o[QStringLiteral("name")] = name;
    o[QStringLiteral("description")] = description;
    o[QStringLiteral("version")] = version;
    o[QStringLiteral("variables")] = QJsonObject::fromVariantMap(variables);
    o[QStringLiteral("runDefaults")] = QJsonObject::fromVariantMap(runDefaults);
    QJsonArray n;
    for (const WorkflowNode &node : nodes) {
        n.append(node.toJson());
    }
    QJsonArray c;
    for (const WorkflowConnection &conn : connections) {
        c.append(conn.toJson());
    }
    o[QStringLiteral("nodes")] = n;
    o[QStringLiteral("connections")] = c;
    return o;
}

QString Workflow::toJsonText() const
{
    return QString::fromUtf8(QJsonDocument(toJson()).toJson(QJsonDocument::Indented));
}

Workflow Workflow::fromJson(const QJsonObject &o, QString *error)
{
    Workflow w;
    w.id = o.value(QStringLiteral("id")).toString();
    if (w.id.isEmpty()) {
        w.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    }
    w.name = o.value(QStringLiteral("name")).toString(QStringLiteral("Untitled"));
    w.description = o.value(QStringLiteral("description")).toString();
    w.version = o.value(QStringLiteral("version")).toInt(1);
    w.variables = o.value(QStringLiteral("variables")).toObject().toVariantMap();
    w.runDefaults = o.value(QStringLiteral("runDefaults")).toObject().toVariantMap();
    const QJsonArray nodes = o.value(QStringLiteral("nodes")).toArray();
    for (const QJsonValue &v : nodes) {
        WorkflowNode n = WorkflowNode::fromJson(v.toObject());
        if (n.id.isEmpty()) {
            n.id = newId();
        }
        if (n.type.isEmpty()) {
            if (error) {
                *error = QStringLiteral("node %1 has no type").arg(n.id);
            }
            continue;
        }
        w.nodes.append(n);
    }
    const QJsonArray conns = o.value(QStringLiteral("connections")).toArray();
    for (const QJsonValue &v : conns) {
        const WorkflowConnection c = WorkflowConnection::fromJson(v.toObject());
        if (!c.from.isEmpty() && !c.to.isEmpty()) {
            w.connections.append(c);
        }
    }
    if (error && w.nodes.isEmpty()) {
        *error = QStringLiteral("workflow has no nodes");
    }
    return w;
}

Workflow Workflow::fromJsonText(const QString &text, QString *error)
{
    QJsonParseError pe;
    const QJsonDocument doc = QJsonDocument::fromJson(text.toUtf8(), &pe);
    if (pe.error != QJsonParseError::NoError) {
        if (error) {
            *error = QStringLiteral("JSON error at %1: %2").arg(pe.offset).arg(pe.errorString());
        }
        return Workflow();
    }
    return fromJson(doc.object(), error);
}

bool Workflow::hasNode(const QString &nodeId) const
{
    for (const WorkflowNode &n : nodes) {
        if (n.id == nodeId) {
            return true;
        }
    }
    return false;
}

WorkflowNode Workflow::node(const QString &nodeId) const
{
    for (const WorkflowNode &n : nodes) {
        if (n.id == nodeId) {
            return n;
        }
    }
    return WorkflowNode();
}

WorkflowNode *Workflow::findNode(const QString &nodeId)
{
    for (WorkflowNode &n : nodes) {
        if (n.id == nodeId) {
            return &n;
        }
    }
    return nullptr;
}

QString Workflow::startNodeId() const
{
    for (const WorkflowNode &n : nodes) {
        if (n.type == QLatin1String("flow.start")) {
            return n.id;
        }
    }
    return nodes.isEmpty() ? QString() : nodes.first().id;
}

QStringList Workflow::nextNodes(const QString &nodeId, const QString &port) const
{
    QStringList list;
    for (const WorkflowConnection &c : connections) {
        if (c.from == nodeId && c.port == port) {
            list << c.to;
        }
    }
    return list;
}

QString Workflow::nextNode(const QString &nodeId, const QString &port) const
{
    const QStringList list = nextNodes(nodeId, port);
    return list.isEmpty() ? QString() : list.first();
}

QString Workflow::addNode(const QString &type, const QPointF &pos, const QVariantMap &params)
{
    WorkflowNode n;
    n.id = newId();
    n.type = type;
    n.pos = pos;
    n.params = NodeCatalog::defaultParams(type);
    for (auto it = params.begin(); it != params.end(); ++it) {
        n.params.insert(it.key(), it.value());
    }
    nodes.append(n);
    return n.id;
}

void Workflow::removeNode(const QString &nodeId)
{
    for (int i = 0; i < nodes.size(); ++i) {
        if (nodes.at(i).id == nodeId) {
            nodes.removeAt(i);
            break;
        }
    }
    for (int i = static_cast<int>(connections.size()) - 1; i >= 0; --i) {
        if (connections.at(i).from == nodeId || connections.at(i).to == nodeId) {
            connections.removeAt(i);
        }
    }
}

void Workflow::connectNodes(const QString &from, const QString &port, const QString &to)
{
    if (from == to) {
        return;
    }
    // One outgoing edge per port (control flow), unless already present.
    for (int i = static_cast<int>(connections.size()) - 1; i >= 0; --i) {
        if (connections.at(i).from == from && connections.at(i).port == port) {
            connections.removeAt(i);
        }
    }
    WorkflowConnection c;
    c.from = from;
    c.port = port;
    c.to = to;
    connections.append(c);
}

void Workflow::disconnect(const QString &from, const QString &port, const QString &to)
{
    for (int i = static_cast<int>(connections.size()) - 1; i >= 0; --i) {
        const WorkflowConnection &c = connections.at(i);
        if (c.from == from && c.port == port && (to.isEmpty() || c.to == to)) {
            connections.removeAt(i);
        }
    }
}

// ---------------------------------------------------------------- validator

QList<ValidationIssue> WorkflowValidator::validate(const Workflow &w)
{
    QList<ValidationIssue> issues;
    auto issue = [&issues](const QString &nodeId, const QString &msg, bool error = true) {
        ValidationIssue i;
        i.nodeId = nodeId;
        i.message = msg;
        i.error = error;
        issues.append(i);
    };
    if (w.name.trimmed().isEmpty()) {
        issue(QString(), QStringLiteral("workflow has no name"));
    }
    int starts = 0;
    QSet<QString> ids;
    for (const WorkflowNode &n : w.nodes) {
        if (ids.contains(n.id)) {
            issue(n.id, QStringLiteral("duplicate node id"));
        }
        ids.insert(n.id);
        if (n.type == QLatin1String("flow.start")) {
            ++starts;
        }
        if (!NodeCatalog::has(n.type)) {
            issue(n.id, QStringLiteral("unknown node type '%1'").arg(n.type));
            continue;
        }
        const NodeSpec spec = NodeCatalog::spec(n.type);
        for (const ParamSpec &p : spec.params) {
            if (p.required) {
                const QVariant v = n.params.value(p.key);
                if (!v.isValid() || (v.typeId() == QMetaType::QString && v.toString().trimmed().isEmpty())) {
                    issue(n.id, QStringLiteral("%1: missing '%2'").arg(spec.title, p.label));
                }
            }
        }
        if (n.onFailure != QLatin1String("fail") && n.onFailure != QLatin1String("continue") && n.onFailure != QLatin1String("stop")) {
            issue(n.id, QStringLiteral("invalid onFailure '%1'").arg(n.onFailure));
        }
    }
    if (starts == 0) {
        issue(QString(), QStringLiteral("no Start node"));
    } else if (starts > 1) {
        issue(QString(), QStringLiteral("more than one Start node"));
    }
    for (const WorkflowConnection &c : w.connections) {
        if (!ids.contains(c.from)) {
            issue(c.from, QStringLiteral("connection from unknown node '%1'").arg(c.from));
        }
        if (!ids.contains(c.to)) {
            issue(c.to, QStringLiteral("connection to unknown node '%1'").arg(c.to));
        }
        if (ids.contains(c.from)) {
            const NodeSpec spec = NodeCatalog::spec(w.node(c.from).type);
            if (!spec.outputs.isEmpty() && !spec.outputs.contains(c.port) && !c.port.startsWith(QLatin1String("case:"))) {
                issue(c.from, QStringLiteral("%1 has no output port '%2'").arg(spec.title, c.port));
            }
        }
    }
    // Unreachable nodes are a warning, dangling required outputs a warning.
    QSet<QString> reachable;
    QStringList stack{ w.startNodeId() };
    while (!stack.isEmpty()) {
        const QString id = stack.takeLast();
        if (id.isEmpty() || reachable.contains(id)) {
            continue;
        }
        reachable.insert(id);
        for (const WorkflowConnection &c : w.connections) {
            if (c.from == id) {
                stack << c.to;
            }
        }
    }
    for (const WorkflowNode &n : w.nodes) {
        if (!reachable.contains(n.id) && n.type != QLatin1String("flow.start")) {
            issue(n.id, QStringLiteral("'%1' is not reachable from Start").arg(n.title.isEmpty() ? NodeCatalog::spec(n.type).title : n.title), false);
        }
    }
    return issues;
}

bool WorkflowValidator::isValid(const Workflow &workflow, QString *firstError)
{
    const QList<ValidationIssue> issues = validate(workflow);
    for (const ValidationIssue &i : issues) {
        if (i.error) {
            if (firstError) {
                *firstError = i.message;
            }
            return false;
        }
    }
    return true;
}

} // namespace farm
