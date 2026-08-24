#ifndef FARM_AUTOMATION_NODECATALOG_H
#define FARM_AUTOMATION_NODECATALOG_H

#include <QList>
#include <QString>
#include <QStringList>
#include <QVariant>
#include <QVariantMap>

namespace farm {

struct ParamSpec
{
    QString key;
    QString label;
    QString type;         // string | text | int | double | bool | enum | file | image | package | keycode | duration | expression | selector | variable
    QVariant defaultValue;
    QStringList options;  // for enum
    QString help;
    bool required = false;
};

struct NodeSpec
{
    QString type;
    QString title;
    QString category;     // Flow, Device, Interaction, Application, Timing, Logic, Variables, ADB, Screen, Files, Logging
    QStringList outputs;  // e.g. {"out"} or {"true","false"} or {"body","done"} or {"found","timeout"}
    QList<ParamSpec> params;
    QString help;
    bool risky = false;   // requires operator confirmation in AI-generated / scheduled bulk runs
    int defaultTimeoutMs = 30000;
};

/**
 * The node vocabulary shared by the validator, the engine, the editor's palette
 * / property panel and the natural-language planner.
 */
class NodeCatalog
{
public:
    static const QList<NodeSpec> &all();
    static bool has(const QString &type);
    static NodeSpec spec(const QString &type);
    static QStringList categories();
    static QList<NodeSpec> inCategory(const QString &category);
    static QVariantMap defaultParams(const QString &type);
    /// Compact description used in the AI system prompt and docs.
    static QString describeForPrompt();
};

} // namespace farm

#endif // FARM_AUTOMATION_NODECATALOG_H
