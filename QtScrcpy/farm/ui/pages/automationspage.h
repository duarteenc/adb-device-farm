#ifndef FARM_UI_AUTOMATIONSPAGE_H
#define FARM_UI_AUTOMATIONSPAGE_H

#include <QPointer>
#include <QStringList>
#include <QWidget>

#include "automation/workflowengine.h"
#include "automation/workflowmodel.h"

class QListWidget;
class QLabel;
class QTableWidget;
class QPlainTextEdit;
class QPushButton;
class QSplitter;

namespace farm {

class WorkflowEditor;

/**
 * Automation Studio: workflow library (new / duplicate / import / export /
 * package / record macro / AI generate), the visual editor, run launcher with
 * target + concurrency selection, and the runs panel with per-device progress
 * and structured logs.
 */
class AutomationsPage : public QWidget
{
    Q_OBJECT
public:
    explicit AutomationsPage(QWidget *parent = nullptr);
    void setTargets(const QStringList &ids);
    bool openWorkflow(const Workflow &workflow, bool markNew);    // false = the operator kept the current edits

signals:
    void selectionNeeded();

private:
    void reloadLibrary();
    void loadSelected();
    bool saveCurrent(bool silent = false);
    void newWorkflow();
    void duplicateWorkflow();
    void deleteWorkflow();
    void importWorkflow();
    void exportWorkflow();
    void exportPackage();
    void generateWithAi();
    void runCurrent();
    void reloadRuns();
    void showRun(AutomationRun *run);
    void refreshRunDevices();
    bool confirmDiscard();
    void selectLibraryRow(const QString &id);    // move the list's current row without firing loadSelected()

    QListWidget *m_library = nullptr;
    WorkflowEditor *m_editor = nullptr;
    QLabel *m_title = nullptr;
    QLabel *m_dirtyLabel = nullptr;
    QListWidget *m_runs = nullptr;
    QTableWidget *m_runDevices = nullptr;
    QPlainTextEdit *m_runLog = nullptr;
    QLabel *m_runSummary = nullptr;
    QPushButton *m_pauseBtn = nullptr;
    QPushButton *m_stopBtn = nullptr;
    QPushButton *m_retryBtn = nullptr;
    QPushButton *m_recordBtn = nullptr;
    QLabel *m_targetsLabel = nullptr;
    QStringList m_targets;
    QString m_currentId;
    QPointer<AutomationRun> m_shownRun;
};

} // namespace farm

#endif // FARM_UI_AUTOMATIONSPAGE_H
