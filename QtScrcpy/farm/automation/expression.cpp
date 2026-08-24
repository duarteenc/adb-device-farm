#include "expression.h"

#include <QRegularExpression>
#include <QStringList>

namespace farm {

QVariant Expression::lookup(const QString &key, const QVariantMap &vars)
{
    const QStringList parts = key.split(QLatin1Char('.'));
    QVariant current = vars.value(parts.first());
    for (int i = 1; i < parts.size() && current.isValid(); ++i) {
        if ((parts.at(i) == QLatin1String("length") || parts.at(i) == QLatin1String("size")) && current.typeId() != QMetaType::QVariantMap) {
            current = current.typeId() == QMetaType::QVariantList ? static_cast<int>(current.toList().size()) : static_cast<int>(current.toString().size());
            continue;
        }
        if (current.typeId() == QMetaType::QVariantMap) {
            current = current.toMap().value(parts.at(i));
        } else if (current.typeId() == QMetaType::QVariantList) {
            bool ok = false;
            const int idx = parts.at(i).toInt(&ok);
            const QVariantList list = current.toList();
            current = (ok && idx >= 0 && idx < list.size()) ? list.at(idx) : QVariant();
        } else {
            current = QVariant();
        }
    }
    return current;
}

QString Expression::displayValue(const QVariant &v)
{
    if (!v.isValid()) {
        return QString();
    }
    if (v.typeId() == QMetaType::Bool) {
        return v.toBool() ? QStringLiteral("true") : QStringLiteral("false");
    }
    if (v.typeId() == QMetaType::QVariantList) {
        QStringList parts;
        for (const QVariant &e : v.toList()) {
            parts << displayValue(e);
        }
        return QStringLiteral("[%1]").arg(parts.join(QStringLiteral(", ")));
    }
    if (v.typeId() == QMetaType::QVariantMap) {
        QStringList parts;
        const QVariantMap m = v.toMap();
        for (auto it = m.begin(); it != m.end(); ++it) {
            parts << QStringLiteral("%1=%2").arg(it.key(), displayValue(it.value()));
        }
        return QStringLiteral("{%1}").arg(parts.join(QStringLiteral(", ")));
    }
    if (v.typeId() == QMetaType::Double) {
        const double d = v.toDouble();
        if (d == static_cast<double>(static_cast<qint64>(d))) {
            return QString::number(static_cast<qint64>(d));
        }
        return QString::number(d, 'g', 10);
    }
    return v.toString();
}

QString Expression::substitute(const QString &text, const QVariantMap &vars)
{
    static const QRegularExpression re(QStringLiteral("\\$\\{([A-Za-z_][A-Za-z0-9_.]*)\\}"));
    QString out;
    int last = 0;
    QRegularExpressionMatchIterator it = re.globalMatch(text);
    while (it.hasNext()) {
        const QRegularExpressionMatch m = it.next();
        out += text.mid(last, m.capturedStart() - last);
        out += displayValue(lookup(m.captured(1), vars));
        last = m.capturedEnd();
    }
    out += text.mid(last);
    return out;
}

namespace {
QVariant autoType(const QString &s)
{
    const QString t = s.trimmed();
    if (t.compare(QLatin1String("true"), Qt::CaseInsensitive) == 0) {
        return true;
    }
    if (t.compare(QLatin1String("false"), Qt::CaseInsensitive) == 0) {
        return false;
    }
    bool ok = false;
    const qlonglong i = t.toLongLong(&ok);
    if (ok) {
        return i;
    }
    const double d = t.toDouble(&ok);
    if (ok) {
        return d;
    }
    if (t.size() >= 2 && ((t.startsWith(QLatin1Char('"')) && t.endsWith(QLatin1Char('"'))) || (t.startsWith(QLatin1Char('\'')) && t.endsWith(QLatin1Char('\''))))) {
        return t.mid(1, t.size() - 2);
    }
    return t;
}

bool isNumber(const QVariant &v, double &out)
{
    switch (v.typeId()) {
    case QMetaType::Int:
    case QMetaType::UInt:
    case QMetaType::LongLong:
    case QMetaType::ULongLong:
    case QMetaType::Double:
    case QMetaType::Float:
        out = v.toDouble();
        return true;
    default:
        break;
    }
    bool ok = false;
    out = v.toString().trimmed().toDouble(&ok);
    return ok && !v.toString().trimmed().isEmpty();
}
} // namespace

QVariant Expression::value(const QString &text, const QVariantMap &vars)
{
    static const QRegularExpression whole(QStringLiteral("^\\s*\\$\\{([A-Za-z_][A-Za-z0-9_.]*)\\}\\s*$"));
    const QRegularExpressionMatch m = whole.match(text);
    if (m.hasMatch()) {
        return lookup(m.captured(1), vars);
    }
    return autoType(substitute(text, vars));
}

bool Expression::toBool(const QVariant &v)
{
    if (v.typeId() == QMetaType::Bool) {
        return v.toBool();
    }
    double d = 0;
    if (isNumber(v, d)) {
        return d != 0.0;
    }
    const QString s = v.toString().trimmed().toLower();
    return !(s.isEmpty() || s == QLatin1String("false") || s == QLatin1String("0") || s == QLatin1String("no"));
}

bool Expression::compare(const QVariant &lhs, const QString &op, const QVariant &rhs)
{
    double a = 0;
    double b = 0;
    const bool numeric = isNumber(lhs, a) && isNumber(rhs, b);
    if (op == QLatin1String("==")) {
        if (numeric) {
            return qFuzzyCompare(a + 1.0, b + 1.0);
        }
        if (lhs.typeId() == QMetaType::Bool || rhs.typeId() == QMetaType::Bool) {
            return toBool(lhs) == toBool(rhs);
        }
        return lhs.toString().compare(rhs.toString(), Qt::CaseInsensitive) == 0;
    }
    if (op == QLatin1String("!=")) {
        return !compare(lhs, QStringLiteral("=="), rhs);
    }
    if (op == QLatin1String("<")) {
        return numeric ? a < b : lhs.toString() < rhs.toString();
    }
    if (op == QLatin1String("<=")) {
        return numeric ? a <= b : lhs.toString() <= rhs.toString();
    }
    if (op == QLatin1String(">")) {
        return numeric ? a > b : lhs.toString() > rhs.toString();
    }
    if (op == QLatin1String(">=")) {
        return numeric ? a >= b : lhs.toString() >= rhs.toString();
    }
    if (op == QLatin1String("contains")) {
        if (lhs.typeId() == QMetaType::QVariantList) {
            for (const QVariant &e : lhs.toList()) {
                if (compare(e, QStringLiteral("=="), rhs)) {
                    return true;
                }
            }
            return false;
        }
        return lhs.toString().contains(rhs.toString(), Qt::CaseInsensitive);
    }
    if (op == QLatin1String("startsWith")) {
        return lhs.toString().startsWith(rhs.toString(), Qt::CaseInsensitive);
    }
    if (op == QLatin1String("endsWith")) {
        return lhs.toString().endsWith(rhs.toString(), Qt::CaseInsensitive);
    }
    if (op == QLatin1String("matches")) {
        return QRegularExpression(rhs.toString()).match(lhs.toString()).hasMatch();
    }
    return false;
}

bool Expression::evaluate(const QString &condition, const QVariantMap &vars, QString *error)
{
    const QString cond = condition.trimmed();
    if (cond.isEmpty()) {
        return false;
    }
    // Split on || first (lowest precedence), then &&.
    const QStringList orParts = cond.split(QStringLiteral("||"));
    if (orParts.size() > 1) {
        for (const QString &p : orParts) {
            if (evaluate(p, vars, error)) {
                return true;
            }
        }
        return false;
    }
    const QStringList andParts = cond.split(QStringLiteral("&&"));
    if (andParts.size() > 1) {
        for (const QString &p : andParts) {
            if (!evaluate(p, vars, error)) {
                return false;
            }
        }
        return true;
    }
    static const QRegularExpression re(QStringLiteral("^(.*?)\\s*(==|!=|<=|>=|<|>|\\bcontains\\b|\\bstartsWith\\b|\\bendsWith\\b|\\bmatches\\b)\\s*(.*)$"));
    const QRegularExpressionMatch m = re.match(cond);
    if (!m.hasMatch()) {
        // bare value / !value
        QString bare = cond;
        bool negate = false;
        if (bare.startsWith(QLatin1Char('!'))) {
            negate = true;
            bare = bare.mid(1).trimmed();
        }
        const bool v = toBool(value(bare, vars));
        return negate ? !v : v;
    }
    const QVariant lhs = value(m.captured(1), vars);
    const QVariant rhs = value(m.captured(3), vars);
    return compare(lhs, m.captured(2).trimmed(), rhs);
}

} // namespace farm
