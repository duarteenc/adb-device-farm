#ifndef FARM_AUTOMATION_EXPRESSION_H
#define FARM_AUTOMATION_EXPRESSION_H

#include <QString>
#include <QVariant>
#include <QVariantMap>

namespace farm {

/**
 * Tiny, safe expression language for workflow parameters and conditions.
 *
 *  - substitute("Hello ${name}", vars)          -> "Hello Bob"
 *    Nested keys: ${match.x}, ${device.model}. Unknown variables become "".
 *  - evaluate("${battery} < 20 && ${charging} == false", vars) -> bool
 *    Operators: == != < <= > >= contains startsWith endsWith matches (regex),
 *    combined with && / || (evaluated left to right, no parentheses), and a
 *    leading ! to negate a bare boolean. Numbers compare numerically when
 *    both sides parse as numbers; "true"/"false" compare as booleans.
 *  - value("${count}") returns the typed variable when the whole string is a
 *    single ${reference}; otherwise the substituted string, auto-typed
 *    (int, double, bool) when it parses as such.
 */
class Expression
{
public:
    static QString substitute(const QString &text, const QVariantMap &vars);
    static bool evaluate(const QString &condition, const QVariantMap &vars, QString *error = nullptr);
    static QVariant value(const QString &text, const QVariantMap &vars);
    static QVariant lookup(const QString &key, const QVariantMap &vars);
    static bool compare(const QVariant &lhs, const QString &op, const QVariant &rhs);
    static bool toBool(const QVariant &v);
    static QString displayValue(const QVariant &v);
};

} // namespace farm

#endif // FARM_AUTOMATION_EXPRESSION_H
