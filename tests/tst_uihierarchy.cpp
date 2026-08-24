#include <QtTest>

#include "automation/uihierarchy.h"

using namespace farm;

class TestUiHierarchy : public QObject
{
    Q_OBJECT
private slots:
    void parseAndSelect()
    {
        const QString xml = QStringLiteral(
            "UI hierchary dumped to: /sdcard/farm_ui.xml\n"
            "<?xml version='1.0' encoding='UTF-8' standalone='yes' ?>"
            "<hierarchy rotation=\"0\">"
            "<node index=\"0\" text=\"\" resource-id=\"\" class=\"android.widget.FrameLayout\" package=\"com.android.settings\" content-desc=\"\" "
            "checkable=\"false\" checked=\"false\" clickable=\"false\" enabled=\"true\" focusable=\"false\" focused=\"false\" scrollable=\"false\" "
            "long-clickable=\"false\" password=\"false\" selected=\"false\" bounds=\"[0,0][1080,2220]\">"
            "<node index=\"0\" text=\"OK\" resource-id=\"android:id/button1\" class=\"android.widget.Button\" package=\"com.android.settings\" "
            "content-desc=\"Confirm\" clickable=\"true\" enabled=\"true\" bounds=\"[100,200][300,280]\"/>"
            "<node index=\"1\" text=\"Cancel\" resource-id=\"android:id/button2\" class=\"android.widget.Button\" package=\"com.android.settings\" "
            "content-desc=\"\" clickable=\"true\" enabled=\"false\" bounds=\"[400,200][600,280]\"/>"
            "<node index=\"2\" text=\"Sign in with Google\" resource-id=\"\" class=\"android.widget.TextView\" package=\"com.android.settings\" "
            "content-desc=\"\" clickable=\"false\" enabled=\"true\" bounds=\"[0,500][1080,560]\"/>"
            "</node></hierarchy>");
        const QList<UiNode> nodes = UiHierarchy::parse(xml);
        QCOMPARE(nodes.size(), 4);
        QCOMPARE(nodes[1].text, QStringLiteral("OK"));
        QCOMPARE(nodes[1].resourceId, QStringLiteral("android:id/button1"));
        QCOMPARE(nodes[1].bounds, QRect(100, 200, 200, 80));
        QCOMPARE(nodes[1].center(), QPoint(199, 239));
        QVERIFY(nodes[1].clickable);
        QVERIFY(!nodes[2].enabled);

        QCOMPARE(UiHierarchy::find(nodes, UiSelector::parse(QStringLiteral("text=ok"))).size(), 1);
        QCOMPARE(UiHierarchy::find(nodes, UiSelector::parse(QStringLiteral("resourceId=button2"))).first().text, QStringLiteral("Cancel"));
        QCOMPARE(UiHierarchy::find(nodes, UiSelector::parse(QStringLiteral("textContains=google"))).size(), 1);
        QCOMPARE(UiHierarchy::find(nodes, UiSelector::parse(QStringLiteral("contentDesc=Confirm"))).first().resourceId, QStringLiteral("android:id/button1"));
        QCOMPARE(UiHierarchy::find(nodes, UiSelector::parse(QStringLiteral("class=Button"))).size(), 2);
        QCOMPARE(UiHierarchy::find(nodes, UiSelector::parse(QStringLiteral("class=Button; clickable=true; instance=1"))).first().text, QStringLiteral("Cancel"));
        QCOMPARE(UiHierarchy::find(nodes, UiSelector::parse(QStringLiteral("Cancel"))).size(), 1);    // bare value = text
        QVERIFY(UiHierarchy::find(nodes, UiSelector::parse(QStringLiteral("class=Button; instance=5"))).isEmpty());    // instance out of range
        QVERIFY(UiHierarchy::find(nodes, UiSelector::parse(QStringLiteral("text=Nope"))).isEmpty());
        QRect r;
        QVERIFY(UiHierarchy::parseBounds(QStringLiteral("[10,20][30,60]"), r));
        QCOMPARE(r, QRect(10, 20, 20, 40));
        QVERIFY(!UiHierarchy::parseBounds(QStringLiteral("garbage"), r));
        QVERIFY(UiHierarchy::parse(QStringLiteral("no xml here")).isEmpty());
        QCOMPARE(UiSelector::parse(QStringLiteral("text=a; id=b")).toString(), QStringLiteral("text=a; resourceId=b"));
    }
};

QTEST_APPLESS_MAIN(TestUiHierarchy)
#include "tst_uihierarchy.moc"
