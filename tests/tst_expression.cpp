#include <QtTest>

#include "automation/expression.h"

using namespace farm;

class TestExpression : public QObject
{
    Q_OBJECT
private slots:
    void substitution()
    {
        QVariantMap vars;
        vars[QStringLiteral("name")] = QStringLiteral("Bob");
        vars[QStringLiteral("count")] = 3;
        QVariantMap match;
        match[QStringLiteral("x")] = 0.5;
        match[QStringLiteral("y")] = 0.25;
        vars[QStringLiteral("match")] = match;
        vars[QStringLiteral("list")] = QVariantList{ 1, 2, 3 };
        QCOMPARE(Expression::substitute(QStringLiteral("Hello ${name}, ${count} devices"), vars), QStringLiteral("Hello Bob, 3 devices"));
        QCOMPARE(Expression::substitute(QStringLiteral("${match.x}/${match.y}"), vars), QStringLiteral("0.5/0.25"));
        QCOMPARE(Expression::substitute(QStringLiteral("${missing}|"), vars), QStringLiteral("|"));
        QCOMPARE(Expression::substitute(QStringLiteral("${list.1} of ${list.length}"), vars), QStringLiteral("2 of 3"));
        QCOMPARE(Expression::value(QStringLiteral("${count}"), vars).toInt(), 3);
        QCOMPARE(Expression::value(QStringLiteral("${match}"), vars).toMap().value(QStringLiteral("x")).toDouble(), 0.5);
        QCOMPARE(Expression::value(QStringLiteral("42"), vars).typeId(), static_cast<int>(QMetaType::LongLong));
        QCOMPARE(Expression::value(QStringLiteral("true"), vars).typeId(), static_cast<int>(QMetaType::Bool));
        QCOMPARE(Expression::value(QStringLiteral("\"quoted\""), vars).toString(), QStringLiteral("quoted"));
    }

    void conditions()
    {
        QVariantMap vars;
        vars[QStringLiteral("battery")] = 15;
        vars[QStringLiteral("charging")] = false;
        vars[QStringLiteral("text")] = QStringLiteral("Login OK");
        vars[QStringLiteral("found")] = true;
        QVERIFY(Expression::evaluate(QStringLiteral("${battery} < 20"), vars));
        QVERIFY(!Expression::evaluate(QStringLiteral("${battery} >= 20"), vars));
        QVERIFY(Expression::evaluate(QStringLiteral("${battery} < 20 && ${charging} == false"), vars));
        QVERIFY(!Expression::evaluate(QStringLiteral("${battery} < 10 && ${charging} == false"), vars));
        QVERIFY(Expression::evaluate(QStringLiteral("${battery} < 10 || ${found} == true"), vars));
        QVERIFY(Expression::evaluate(QStringLiteral("${text} contains \"OK\""), vars));
        QVERIFY(Expression::evaluate(QStringLiteral("${text} startsWith Login"), vars));
        QVERIFY(Expression::evaluate(QStringLiteral("${text} matches ^Login.*"), vars));
        QVERIFY(Expression::evaluate(QStringLiteral("${found}"), vars));
        QVERIFY(!Expression::evaluate(QStringLiteral("!${found}"), vars));
        QVERIFY(!Expression::evaluate(QStringLiteral("${missing}"), vars));
        QVERIFY(Expression::evaluate(QStringLiteral("${battery} == 15"), vars));
        QVERIFY(Expression::evaluate(QStringLiteral("${text} != Logout"), vars));
        QVERIFY(Expression::evaluate(QStringLiteral("abc == ABC"), vars));    // strings compare case-insensitively
        QVERIFY(!Expression::evaluate(QString(), vars));
    }
};

QTEST_APPLESS_MAIN(TestExpression)
#include "tst_expression.moc"
