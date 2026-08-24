#include "uihierarchy.h"

#include <QRegularExpression>
#include <QXmlStreamReader>

namespace farm {

UiSelector UiSelector::parse(const QString &spec)
{
    UiSelector s;
    const QStringList parts = spec.split(QLatin1Char(';'), Qt::SkipEmptyParts);
    for (const QString &partRaw : parts) {
        const QString part = partRaw.trimmed();
        const int eq = part.indexOf(QLatin1Char('='));
        if (eq <= 0) {
            // bare value = text
            s.text = part;
            continue;
        }
        const QString key = part.left(eq).trimmed().toLower();
        const QString value = part.mid(eq + 1).trimmed();
        if (key == QLatin1String("text")) {
            s.text = value;
        } else if (key == QLatin1String("textcontains")) {
            s.textContains = value;
        } else if (key == QLatin1String("resourceid") || key == QLatin1String("id")) {
            s.resourceId = value;
        } else if (key == QLatin1String("contentdesc") || key == QLatin1String("desc")) {
            s.contentDesc = value;
        } else if (key == QLatin1String("contentdesccontains") || key == QLatin1String("desccontains")) {
            s.contentDescContains = value;
        } else if (key == QLatin1String("class") || key == QLatin1String("classname")) {
            s.className = value;
        } else if (key == QLatin1String("clickable")) {
            s.clickableOnly = value.compare(QLatin1String("true"), Qt::CaseInsensitive) == 0;
        } else if (key == QLatin1String("instance")) {
            s.instance = value.toInt();
        }
    }
    return s;
}

bool UiSelector::matches(const UiNode &n) const
{
    if (!text.isEmpty() && n.text.compare(text, Qt::CaseInsensitive) != 0) {
        return false;
    }
    if (!textContains.isEmpty() && !n.text.contains(textContains, Qt::CaseInsensitive)) {
        return false;
    }
    if (!resourceId.isEmpty() && n.resourceId != resourceId && !n.resourceId.endsWith(QStringLiteral(":id/") + resourceId)) {
        return false;
    }
    if (!contentDesc.isEmpty() && n.contentDesc.compare(contentDesc, Qt::CaseInsensitive) != 0) {
        return false;
    }
    if (!contentDescContains.isEmpty() && !n.contentDesc.contains(contentDescContains, Qt::CaseInsensitive)) {
        return false;
    }
    if (!className.isEmpty() && n.className != className && !n.className.endsWith(QLatin1Char('.') + className)) {
        return false;
    }
    if (clickableOnly && !n.clickable) {
        return false;
    }
    return true;
}

QString UiSelector::toString() const
{
    QStringList parts;
    if (!text.isEmpty()) {
        parts << QStringLiteral("text=%1").arg(text);
    }
    if (!textContains.isEmpty()) {
        parts << QStringLiteral("textContains=%1").arg(textContains);
    }
    if (!resourceId.isEmpty()) {
        parts << QStringLiteral("resourceId=%1").arg(resourceId);
    }
    if (!contentDesc.isEmpty()) {
        parts << QStringLiteral("contentDesc=%1").arg(contentDesc);
    }
    if (!contentDescContains.isEmpty()) {
        parts << QStringLiteral("contentDescContains=%1").arg(contentDescContains);
    }
    if (!className.isEmpty()) {
        parts << QStringLiteral("class=%1").arg(className);
    }
    if (clickableOnly) {
        parts << QStringLiteral("clickable=true");
    }
    if (instance > 0) {
        parts << QStringLiteral("instance=%1").arg(instance);
    }
    return parts.join(QStringLiteral("; "));
}

QString UiHierarchy::dumpScript()
{
    // --compressed keeps only meaningful nodes; fall back to the plain dump on old uiautomator.
    return QStringLiteral("(uiautomator dump --compressed /sdcard/farm_ui.xml >/dev/null 2>&1 || uiautomator dump /sdcard/farm_ui.xml >/dev/null 2>&1) && cat /sdcard/farm_ui.xml && rm -f /sdcard/farm_ui.xml");
}

bool UiHierarchy::parseBounds(const QString &bounds, QRect &out)
{
    static const QRegularExpression re(QStringLiteral("\\[(-?\\d+),(-?\\d+)\\]\\[(-?\\d+),(-?\\d+)\\]"));
    const QRegularExpressionMatch m = re.match(bounds);
    if (!m.hasMatch()) {
        return false;
    }
    const int x1 = m.captured(1).toInt();
    const int y1 = m.captured(2).toInt();
    const int x2 = m.captured(3).toInt();
    const int y2 = m.captured(4).toInt();
    out = QRect(QPoint(x1, y1), QPoint(x2 - 1, y2 - 1));
    return true;
}

QList<UiNode> UiHierarchy::parse(const QString &xmlIn)
{
    QList<UiNode> nodes;
    // Some devices print "UI hierchary dumped to: ..." before/after the XML.
    const int start = xmlIn.indexOf(QLatin1String("<?xml"));
    const int startAlt = xmlIn.indexOf(QLatin1String("<hierarchy"));
    const int begin = start >= 0 ? start : startAlt;
    if (begin < 0) {
        return nodes;
    }
    QString xml = xmlIn.mid(begin);
    const int end = xml.lastIndexOf(QLatin1String("</hierarchy>"));
    if (end > 0) {
        xml = xml.left(end + 12);
    }
    QXmlStreamReader reader(xml);
    while (!reader.atEnd()) {
        reader.readNext();
        if (!reader.isStartElement() || reader.name() != QLatin1String("node")) {
            continue;
        }
        const QXmlStreamAttributes a = reader.attributes();
        UiNode n;
        n.index = a.value(QLatin1String("index")).toInt();
        n.text = a.value(QLatin1String("text")).toString();
        n.contentDesc = a.value(QLatin1String("content-desc")).toString();
        n.resourceId = a.value(QLatin1String("resource-id")).toString();
        n.className = a.value(QLatin1String("class")).toString();
        n.package = a.value(QLatin1String("package")).toString();
        n.clickable = a.value(QLatin1String("clickable")) == QLatin1String("true");
        n.enabled = a.value(QLatin1String("enabled")) != QLatin1String("false");
        n.checked = a.value(QLatin1String("checked")) == QLatin1String("true");
        n.focused = a.value(QLatin1String("focused")) == QLatin1String("true");
        n.scrollable = a.value(QLatin1String("scrollable")) == QLatin1String("true");
        n.selected = a.value(QLatin1String("selected")) == QLatin1String("true");
        parseBounds(a.value(QLatin1String("bounds")).toString(), n.bounds);
        nodes.append(n);
    }
    return nodes;
}

QList<UiNode> UiHierarchy::find(const QList<UiNode> &nodes, const UiSelector &selector)
{
    QList<UiNode> out;
    for (const UiNode &n : nodes) {
        if (selector.matches(n)) {
            out.append(n);
        }
    }
    if (selector.instance > 0) {
        if (selector.instance < out.size()) {
            return QList<UiNode>{ out.at(selector.instance) };
        }
        return QList<UiNode>();    // the requested instance does not exist
    }
    return out;
}

} // namespace farm
