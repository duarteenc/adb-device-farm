#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

#include "automation/nodecatalog.h"
#include "automation/workflowengine.h"
#include "automation/workflowmodel.h"
#include "core/farmsettings.h"
#include "storage/database.h"

using namespace farm;

class TestWorkflow : public QObject
{
    Q_OBJECT
    QTemporaryDir m_dir;

private slots:
    void initTestCase()
    {
        QVERIFY(m_dir.isValid());
        FarmSettings::instance().setDataDirectory(m_dir.path());
        QVERIFY(Database::instance().open(m_dir.filePath(QStringLiteral("farm.db"))));
    }

    void catalog()
    {
        QVERIFY(NodeCatalog::all().size() > 40);
        QVERIFY(NodeCatalog::has(QStringLiteral("input.tap")));
        QVERIFY(!NodeCatalog::has(QStringLiteral("nope")));
        const NodeSpec ifSpec = NodeCatalog::spec(QStringLiteral("logic.if"));
        QCOMPARE(ifSpec.outputs, (QStringList{ QStringLiteral("true"), QStringLiteral("false") }));
        QVERIFY(NodeCatalog::spec(QStringLiteral("app.uninstall")).risky);
        QVERIFY(NodeCatalog::categories().contains(QStringLiteral("Screen")));
        QCOMPARE(NodeCatalog::defaultParams(QStringLiteral("time.wait")).value(QStringLiteral("ms")).toInt(), 1000);
    }

    void modelRoundTrip()
    {
        Workflow w = Workflow::makeEmpty(QStringLiteral("Test"));
        QCOMPARE(w.nodes.size(), 2);
        QCOMPARE(w.connections.size(), 1);
        const QString start = w.startNodeId();
        const QString end = w.nextNode(start, QStringLiteral("out"));
        QVERIFY(!end.isEmpty());
        const QString wait = w.addNode(QStringLiteral("time.wait"), QPointF(100, 100), { { QStringLiteral("ms"), 5 } });
        w.connectNodes(start, QStringLiteral("out"), wait);    // replaces the previous out edge
        w.connectNodes(wait, QStringLiteral("out"), end);
        QCOMPARE(w.connections.size(), 2);
        QCOMPARE(w.nextNode(start, QStringLiteral("out")), wait);
        const QString json = w.toJsonText();
        QString err;
        const Workflow back = Workflow::fromJsonText(json, &err);
        QVERIFY(err.isEmpty());
        QCOMPARE(back.nodes.size(), 3);
        QCOMPARE(back.node(wait).params.value(QStringLiteral("ms")).toInt(), 5);
        QCOMPARE(back.nextNode(wait, QStringLiteral("out")), end);
        QVERIFY(WorkflowValidator::isValid(back));
        w.removeNode(wait);
        QCOMPARE(w.nodes.size(), 2);
        QCOMPARE(w.connections.size(), 0);
    }

    void validation()
    {
        Workflow w;
        w.name = QStringLiteral("Broken");
        WorkflowNode n;
        n.id = QStringLiteral("a");
        n.type = QStringLiteral("no.such.node");
        w.nodes.append(n);
        WorkflowNode tap;
        tap.id = QStringLiteral("b");
        tap.type = QStringLiteral("app.launch");    // package required
        w.nodes.append(tap);
        WorkflowConnection c;
        c.from = QStringLiteral("a");
        c.to = QStringLiteral("zzz");
        w.connections.append(c);
        const QList<ValidationIssue> issues = WorkflowValidator::validate(w);
        QStringList messages;
        for (const ValidationIssue &i : issues) {
            messages << i.message;
        }
        QVERIFY(messages.join(QLatin1Char('|')).contains(QStringLiteral("no Start node")));
        QVERIFY(messages.join(QLatin1Char('|')).contains(QStringLiteral("unknown node type")));
        QVERIFY(messages.join(QLatin1Char('|')).contains(QStringLiteral("missing 'Package'")));
        QVERIFY(messages.join(QLatin1Char('|')).contains(QStringLiteral("unknown node 'zzz'")));
        QVERIFY(!WorkflowValidator::isValid(w));
        QVERIFY(Workflow::fromJsonText(QStringLiteral("{ not json"), nullptr).nodes.isEmpty());
    }

    void engineRunsLogicOnly()
    {
        // No ADB in tests: a workflow made only of logic/variable/timing/log nodes
        // still exercises the interpreter (loops, if, break, sub-flow, variables).
        Workflow w;
        w.id = QStringLiteral("wf-logic");
        w.name = QStringLiteral("Logic");
        const QString start = w.addNode(QStringLiteral("flow.start"), QPointF());
        const QString setV = w.addNode(QStringLiteral("var.set"), QPointF(), { { QStringLiteral("name"), QStringLiteral("total") }, { QStringLiteral("value"), QStringLiteral("0") } });
        const QString loop = w.addNode(QStringLiteral("logic.loop"), QPointF(), { { QStringLiteral("count"), 5 }, { QStringLiteral("indexVariable"), QStringLiteral("i") } });
        const QString inc = w.addNode(QStringLiteral("var.increment"), QPointF(), { { QStringLiteral("name"), QStringLiteral("total") }, { QStringLiteral("by"), 2 } });
        const QString cond = w.addNode(QStringLiteral("logic.if"), QPointF(), { { QStringLiteral("condition"), QStringLiteral("${i} >= 2") } });
        const QString brk = w.addNode(QStringLiteral("logic.break"), QPointF());
        const QString wait = w.addNode(QStringLiteral("time.wait"), QPointF(), { { QStringLiteral("ms"), 5 } });
        const QString check = w.addNode(QStringLiteral("logic.if"), QPointF(), { { QStringLiteral("condition"), QStringLiteral("${total} == 6") } });
        const QString end = w.addNode(QStringLiteral("flow.end"), QPointF());
        const QString failNode = w.addNode(QStringLiteral("flow.fail"), QPointF(), { { QStringLiteral("message"), QStringLiteral("total was ${total}") } });
        w.connectNodes(start, QStringLiteral("out"), setV);
        w.connectNodes(setV, QStringLiteral("out"), loop);
        w.connectNodes(loop, QStringLiteral("body"), inc);
        w.connectNodes(inc, QStringLiteral("out"), cond);
        w.connectNodes(cond, QStringLiteral("true"), brk);
        w.connectNodes(cond, QStringLiteral("false"), wait);
        // wait has no outgoing edge -> returns to the loop node
        w.connectNodes(loop, QStringLiteral("done"), check);
        w.connectNodes(check, QStringLiteral("true"), end);
        w.connectNodes(check, QStringLiteral("false"), failNode);
        QVERIFY(WorkflowValidator::isValid(w));

        AutomationRun *run = WorkflowEngine::instance().start(w, { QStringLiteral("dev-a"), QStringLiteral("dev-b"), QStringLiteral("dev-c") }, 2, QStringLiteral("test"));
        QSignalSpy finished(run, &AutomationRun::finished);
        QVERIFY(finished.wait(15000));
        QCOMPARE(run->status(), AutomationRun::Completed);
        QCOMPARE(run->succeeded(), 3);
        QCOMPARE(run->failed(), 0);
        // i=0: total 2, i=1: total 4, i=2: total 6 -> break -> check true
        QVERIFY(run->logs().size() >= 9);
        bool sawIteration3 = false;
        bool sawIteration5 = false;
        for (const JobLogRow &l : run->logs()) {
            if (l.message == QLatin1String("iteration 3")) {
                sawIteration3 = true;
            }
            if (l.message == QLatin1String("iteration 5")) {
                sawIteration5 = true;
            }
        }
        QVERIFY(sawIteration3);
        QVERIFY(!sawIteration5);    // break left the loop early
        QVERIFY(QFile::exists(run->runDirectory() + QStringLiteral("/logs.json")));
        QVERIFY(QFile::exists(run->runDirectory() + QStringLiteral("/workflow.json")));
        QCOMPARE(RunRepository::loadRecent().size(), 1);
        QVERIFY(RunRepository::loadLogs(run->id()).size() >= 9);
    }

    void engineFailureIsolation()
    {
        // One device fails (flow.fail), the other completes; the run reports both.
        Workflow w;
        w.id = QStringLiteral("wf-fail");
        w.name = QStringLiteral("Fail on b");
        const QString start = w.addNode(QStringLiteral("flow.start"), QPointF());
        const QString prop = w.addNode(QStringLiteral("device.property"), QPointF(), { { QStringLiteral("property"), QStringLiteral("id") }, { QStringLiteral("variable"), QStringLiteral("me") } });
        const QString cond = w.addNode(QStringLiteral("logic.if"), QPointF(), { { QStringLiteral("condition"), QStringLiteral("${me} == dev-b") } });
        const QString failNode = w.addNode(QStringLiteral("flow.fail"), QPointF(), { { QStringLiteral("message"), QStringLiteral("boom") } });
        const QString end = w.addNode(QStringLiteral("flow.end"), QPointF());
        w.connectNodes(start, QStringLiteral("out"), prop);
        w.connectNodes(prop, QStringLiteral("out"), cond);
        w.connectNodes(cond, QStringLiteral("true"), failNode);
        w.connectNodes(cond, QStringLiteral("false"), end);
        FarmSettings::instance().setValue(QStringLiteral("automation/errorScreenshots"), false);    // no adb in tests
        AutomationRun *run = WorkflowEngine::instance().start(w, { QStringLiteral("dev-a"), QStringLiteral("dev-b") }, 4, QStringLiteral("test"));
        QSignalSpy finished(run, &AutomationRun::finished);
        QVERIFY(finished.wait(15000));
        QCOMPARE(run->status(), AutomationRun::Completed);    // partial failure: run completes, device b failed
        QCOMPARE(run->succeeded(), 1);
        QCOMPARE(run->failed(), 1);
        QCOMPARE(run->failedIds(), QStringList{ QStringLiteral("dev-b") });
        QVERIFY(run->progress(QStringLiteral("dev-b")).error.contains(QStringLiteral("boom")));
        // retry failed only re-runs dev-b (still fails deterministically)
        QSignalSpy finished2(run, &AutomationRun::finished);
        run->retryFailed();
        QVERIFY(finished2.wait(15000));
        QCOMPARE(run->progress(QStringLiteral("dev-b")).attempts, 2);
        QCOMPARE(run->progress(QStringLiteral("dev-a")).attempts, 1);
    }

    void engineCancel()
    {
        Workflow w;
        w.id = QStringLiteral("wf-cancel");
        w.name = QStringLiteral("Long wait");
        const QString start = w.addNode(QStringLiteral("flow.start"), QPointF());
        const QString wait = w.addNode(QStringLiteral("time.wait"), QPointF(), { { QStringLiteral("ms"), 20000 } });
        const QString end = w.addNode(QStringLiteral("flow.end"), QPointF());
        w.connectNodes(start, QStringLiteral("out"), wait);
        w.connectNodes(wait, QStringLiteral("out"), end);
        AutomationRun *run = WorkflowEngine::instance().start(w, { QStringLiteral("dev-a"), QStringLiteral("dev-b"), QStringLiteral("dev-c") }, 1, QStringLiteral("test"));
        QSignalSpy finished(run, &AutomationRun::finished);
        QTest::qWait(300);
        QCOMPARE(run->running(), 1);
        run->cancel();
        QVERIFY(finished.wait(5000));
        QCOMPARE(run->status(), AutomationRun::Cancelled);
        QCOMPARE(run->succeeded(), 0);
    }

    void cleanupTestCase()
    {
        Database::instance().close();
    }
};

QTEST_GUILESS_MAIN(TestWorkflow)
#include "tst_workflow.moc"
