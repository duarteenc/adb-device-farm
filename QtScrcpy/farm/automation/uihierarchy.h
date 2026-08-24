#ifndef FARM_AUTOMATION_UIHIERARCHY_H
#define FARM_AUTOMATION_UIHIERARCHY_H

#include <QList>
#include <QRect>
#include <QString>

namespace farm {

struct UiNode
{
    int index = 0;
    QString text;
    QString contentDesc;
    QString resourceId;
    QString className;
    QString package;
    QRect bounds;
    bool clickable = false;
    bool enabled = true;
    bool checked = false;
    bool focused = false;
    bool scrollable = false;
    bool selected = false;
    QPoint center() const { return bounds.center(); }
};

/**
 * Selector syntax (semicolon separated): text=OK; textContains=Sign; resourceId=com.app:id/btn;
 * contentDesc=Menu; contentDescContains=Men; class=android.widget.Button; clickable=true; instance=1
 */
struct UiSelector
{
    QString text;
    QString textContains;
    QString resourceId;
    QString contentDesc;
    QString contentDescContains;
    QString className;
    bool clickableOnly = false;
    int instance = 0;

    static UiSelector parse(const QString &spec);
    bool matches(const UiNode &node) const;
    bool isEmpty() const { return text.isEmpty() && textContains.isEmpty() && resourceId.isEmpty() && contentDesc.isEmpty() && contentDescContains.isEmpty() && className.isEmpty(); }
    QString toString() const;
};

/**
 * Android UI hierarchy via `uiautomator dump` — element-based automation that is
 * robust against pixel changes.
 */
class UiHierarchy
{
public:
    /// Shell script producing the XML on stdout.
    static QString dumpScript();
    static QList<UiNode> parse(const QString &xml);
    static QList<UiNode> find(const QList<UiNode> &nodes, const UiSelector &selector);
    static bool parseBounds(const QString &bounds, QRect &out);
};

} // namespace farm

#endif // FARM_AUTOMATION_UIHIERARCHY_H
