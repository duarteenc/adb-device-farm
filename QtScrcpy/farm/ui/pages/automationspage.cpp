#include "automationspage.h"

#include <QDateTime>
#include <QFile>
#include <QLineEdit>
#include <QRegularExpression>
#include <QUuid>
#include <QComboBox>
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QSplitter>
#include <QTableWidget>
#include <QUrl>
#include <QVBoxLayout>

#include "automation/aiprovider.h"
#include "automation/macrorecorder.h"
#include "automation/nodecatalog.h"
#include "core/farmsettings.h"
#include "devices/deviceregistry.h"
#include "storage/repositories.h"
#include "ui/automation/workfloweditor.h"
#include "ui/farmtheme.h"

namespace farm {

AutomationsPage::AutomationsPage(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("page"));
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(14, 12, 14, 12);
    root->setSpacing(8);

    // ---- toolbar ----
    auto *bar = new QHBoxLayout();
    m_title = new QLabel(tr("Automations"), this);
    m_title->setObjectName(QStringLiteral("pageTitle"));
    m_dirtyLabel = theme::hint(QString(), this);
    bar->addWidget(m_title);
    bar->addWidget(m_dirtyLabel);
    bar->addStretch(1);
    auto *saveBtn = theme::button(tr("Save"), this, QStringLiteral("primary"));
    auto *runBtn = theme::button(tr("Run…"), this, QStringLiteral("primary"));
    auto *undoBtn = theme::button(tr("Undo"), this);
    auto *redoBtn = theme::button(tr("Redo"), this);
    auto *fitBtn = theme::button(tr("Fit"), this);
    auto *validateBtn = theme::button(tr("Validate"), this);
    m_recordBtn = theme::button(tr("● Record macro"), this);
    m_recordBtn->setCheckable(true);
    auto *aiBtn = theme::button(tr("Generate with AI…"), this);
    for (QPushButton *b : { saveBtn, runBtn, undoBtn, redoBtn, fitBtn, validateBtn, m_recordBtn, aiBtn }) {
        bar->addWidget(b);
    }
    root->addLayout(bar);
    m_targetsLabel = theme::hint(tr("Run targets: current selection on the Devices page"), this);
    root->addWidget(m_targetsLabel);

    // ---- body ----
    auto *split = new QSplitter(Qt::Vertical, this);
    auto *top = new QWidget(split);
    auto *tl = new QHBoxLayout(top);
    tl->setContentsMargins(0, 0, 0, 0);
    tl->setSpacing(6);
    auto *lib = new QWidget(top);
    lib->setFixedWidth(210);
    auto *ll = new QVBoxLayout(lib);
    ll->setContentsMargins(0, 0, 0, 0);
    ll->addWidget(theme::sectionTitle(tr("Workflows"), lib));
    m_library = new QListWidget(lib);
    ll->addWidget(m_library, 1);
    auto libBtn = [&](const QString &label, std::function<void()> fn, const QString &role = QString()) {
        auto *b = theme::button(label, lib, role);
        connect(b, &QPushButton::clicked, this, fn);
        ll->addWidget(b);
    };
    libBtn(tr("New"), [this]() { newWorkflow(); }, QStringLiteral("primary"));
    libBtn(tr("Duplicate"), [this]() { duplicateWorkflow(); });
    libBtn(tr("Import JSON…"), [this]() { importWorkflow(); });
    libBtn(tr("Export JSON…"), [this]() { exportWorkflow(); });
    libBtn(tr("Export package…"), [this]() { exportPackage(); });
    libBtn(tr("Delete"), [this]() { deleteWorkflow(); }, QStringLiteral("danger"));
    tl->addWidget(lib);
    m_editor = new WorkflowEditor(top);
    tl->addWidget(m_editor, 1);
    split->addWidget(top);

    // ---- runs panel ----
    auto *runsW = new QWidget(split);
    auto *rl = new QHBoxLayout(runsW);
    rl->setContentsMargins(0, 0, 0, 0);
    rl->setSpacing(6);
    auto *runsList = new QVBoxLayout();
    runsList->addWidget(theme::sectionTitle(tr("Runs"), runsW));
    m_runs = new QListWidget(runsW);
    m_runs->setFixedWidth(300);
    runsList->addWidget(m_runs, 1);
    auto *runBtns = new QHBoxLayout();
    m_pauseBtn = theme::button(tr("Pause"), runsW);
    m_stopBtn = theme::button(tr("Stop"), runsW, QStringLiteral("danger"));
    m_retryBtn = theme::button(tr("Retry failed"), runsW);
    auto *openBtn = theme::button(tr("Folder"), runsW);
    auto *clearBtn = theme::button(tr("Clear finished"), runsW, QStringLiteral("quiet"));
    for (QPushButton *b : { m_pauseBtn, m_stopBtn, m_retryBtn, openBtn }) {
        runBtns->addWidget(b);
    }
    runsList->addLayout(runBtns);
    runsList->addWidget(clearBtn);
    rl->addLayout(runsList);
    auto *runDetail = new QVBoxLayout();
    m_runSummary = new QLabel(runsW);
    m_runSummary->setStyleSheet(QStringLiteral("font-weight:bold;"));
    runDetail->addWidget(m_runSummary);
    m_runDevices = new QTableWidget(0, 6, runsW);
    m_runDevices->setHorizontalHeaderLabels({ tr("#"), tr("Device"), tr("Status"), tr("Step"), tr("Current node"), tr("Error") });
    m_runDevices->horizontalHeader()->setSectionResizeMode(5, QHeaderView::Stretch);
    m_runDevices->verticalHeader()->setVisible(false);
    m_runDevices->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_runDevices->setSelectionBehavior(QAbstractItemView::SelectRows);
    runDetail->addWidget(m_runDevices, 2);
    m_runLog = new QPlainTextEdit(runsW);
    m_runLog->setReadOnly(true);
    m_runLog->setMaximumBlockCount(5000);
    m_runLog->setFont(QFont(QStringLiteral("Consolas"), 9));
    runDetail->addWidget(m_runLog, 1);
    rl->addLayout(runDetail, 1);
    split->addWidget(runsW);
    split->setStretchFactor(0, 3);
    split->setStretchFactor(1, 2);
    root->addWidget(split, 1);

    // ---- wiring ----
    connect(m_library, &QListWidget::currentRowChanged, this, [this](int) { loadSelected(); });
    connect(saveBtn, &QPushButton::clicked, this, [this]() { saveCurrent(); });
    connect(runBtn, &QPushButton::clicked, this, &AutomationsPage::runCurrent);
    connect(undoBtn, &QPushButton::clicked, this, [this]() { m_editor->scene()->undo(); });
    connect(redoBtn, &QPushButton::clicked, this, [this]() { m_editor->scene()->redo(); });
    connect(fitBtn, &QPushButton::clicked, this, [this]() { m_editor->fitAll(); });
    connect(validateBtn, &QPushButton::clicked, this, [this]() { m_editor->validate(); });
    connect(aiBtn, &QPushButton::clicked, this, &AutomationsPage::generateWithAi);
    connect(m_editor, &WorkflowEditor::modified, this, [this]() { m_dirtyLabel->setText(tr("● unsaved changes")); });
    connect(m_recordBtn, &QPushButton::toggled, this, [this](bool on) {
        MacroRecorder &rec = MacroRecorder::instance();
        if (on) {
            const QString master = m_targets.isEmpty() ? QString() : m_targets.first();
            rec.start(master);
            m_recordBtn->setText(tr("■ Stop recording (open host mode on the Devices page and interact)"));
        } else {
            rec.stop();
            m_recordBtn->setText(tr("● Record macro"));
            if (rec.eventCount() > 0) {
                bool ok = false;
                const QString name = QInputDialog::getText(this, tr("Recorded macro"), tr("%n event(s) recorded. Workflow name:", nullptr, rec.eventCount()), QLineEdit::Normal, tr("Macro %1").arg(QDateTime::currentDateTime().toString(QStringLiteral("HH:mm"))), &ok);
                if (ok && !name.isEmpty()) {
                    openWorkflow(rec.toWorkflow(name), true);
                }
            }
        }
    });
    connect(&MacroRecorder::instance(), &MacroRecorder::recordingChanged, this, [this](bool on) {
        if (m_recordBtn->isChecked() != on) {
            m_recordBtn->setChecked(on);
        }
    });
    connect(&MacroRecorder::instance(), &MacroRecorder::eventRecorded, this, [this](int n) {
        if (MacroRecorder::instance().isRecording()) {
            m_recordBtn->setText(tr("■ Stop recording (%n event(s))", nullptr, n));
        }
    });
    connect(m_runs, &QListWidget::currentRowChanged, this, [this](int row) {
        if (row < 0) {
            showRun(nullptr);
            return;
        }
        showRun(WorkflowEngine::instance().run(m_runs->item(row)->data(Qt::UserRole).toString()));
    });
    connect(&WorkflowEngine::instance(), &WorkflowEngine::runsChanged, this, &AutomationsPage::reloadRuns);
    connect(&WorkflowEngine::instance(), &WorkflowEngine::runAdded, this, [this](AutomationRun *run) {
        reloadRuns();
        m_runs->setCurrentRow(0);
        showRun(run);
    });
    connect(m_pauseBtn, &QPushButton::clicked, this, [this]() {
        if (m_shownRun) {
            if (m_shownRun->status() == AutomationRun::Paused) {
                m_shownRun->resume();
            } else {
                m_shownRun->pause();
            }
        }
    });
    connect(m_stopBtn, &QPushButton::clicked, this, [this]() {
        if (m_shownRun) {
            m_shownRun->stop();
        }
    });
    connect(m_retryBtn, &QPushButton::clicked, this, [this]() {
        if (m_shownRun) {
            m_shownRun->retryFailed();
        }
    });
    connect(openBtn, &QPushButton::clicked, this, [this]() {
        if (m_shownRun) {
            QDesktopServices::openUrl(QUrl::fromLocalFile(m_shownRun->runDirectory()));
        }
    });
    connect(clearBtn, &QPushButton::clicked, this, []() { WorkflowEngine::instance().clearFinished(); });
    connect(m_runDevices, &QTableWidget::itemDoubleClicked, this, [this](QTableWidgetItem *it) {
        if (!m_shownRun) {
            return;
        }
        const AutomationRun::DeviceProgress p = m_shownRun->progress(m_runDevices->item(it->row(), 1)->data(Qt::UserRole).toString());
        if (!p.errorScreenshot.isEmpty()) {
            QDesktopServices::openUrl(QUrl::fromLocalFile(p.errorScreenshot));
        }
    });

    reloadLibrary();
    reloadRuns();
    if (m_library->count() == 0) {
        // Ship a starter workflow so the editor is never empty on first use.
        Workflow w = Workflow::makeEmpty(tr("Example: open Settings and screenshot"));
        const QString start = w.startNodeId();
        const QString end = w.nextNode(start, QStringLiteral("out"));
        w.disconnect(start, QStringLiteral("out"), end);
        const QString launch = w.addNode(QStringLiteral("app.launch"), QPointF(230, 120), { { QStringLiteral("package"), QStringLiteral("com.android.settings") } });
        const QString wait = w.addNode(QStringLiteral("time.wait"), QPointF(420, 120), { { QStringLiteral("ms"), 1500 } });
        const QString shot = w.addNode(QStringLiteral("screen.screenshot"), QPointF(610, 120), { { QStringLiteral("tag"), QStringLiteral("settings") } });
        const QString home = w.addNode(QStringLiteral("input.home"), QPointF(800, 120));
        w.connectNodes(start, QStringLiteral("out"), launch);
        w.connectNodes(launch, QStringLiteral("out"), wait);
        w.connectNodes(wait, QStringLiteral("out"), shot);
        w.connectNodes(shot, QStringLiteral("out"), home);
        w.connectNodes(home, QStringLiteral("out"), end);
        if (WorkflowNode *e = w.findNode(end)) {
            e->pos = QPointF(990, 120);
        }
        WorkflowRow row;
        row.id = w.id;
        row.name = w.name;
        row.json = w.toJsonText();
        WorkflowRepository::save(row);
        reloadLibrary();
    }
    if (m_library->count() > 0) {
        m_library->setCurrentRow(0);
    }
}

void AutomationsPage::setTargets(const QStringList &ids)
{
    m_targets = ids;
    m_targetsLabel->setText(ids.isEmpty() ? tr("Run targets: none selected — choose devices on the Devices page, or pick a group / all online in the Run dialog.")
                                          : tr("Run targets: %n selected device(s)", nullptr, static_cast<int>(ids.size())));
}

void AutomationsPage::reloadLibrary()
{
    const QString prev = m_currentId;
    m_library->blockSignals(true);
    m_library->clear();
    int select = -1;
    int row = 0;
    for (const WorkflowRow &w : WorkflowRepository::loadAll()) {
        auto *item = new QListWidgetItem(w.name, m_library);
        item->setData(Qt::UserRole, w.id);
        item->setToolTip(tr("Updated %1").arg(w.updated.toString(QStringLiteral("yyyy-MM-dd HH:mm"))));
        if (w.id == prev) {
            select = row;
        }
        ++row;
    }
    m_library->blockSignals(false);
    if (select >= 0) {
        m_library->setCurrentRow(select);
    }
}

bool AutomationsPage::confirmDiscard()
{
    if (!m_editor->isDirty()) {
        return true;
    }
    const auto r = QMessageBox::question(this, tr("Unsaved changes"), tr("Save changes to '%1'?").arg(m_editor->workflow().name), QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
    if (r == QMessageBox::Cancel) {
        return false;
    }
    if (r == QMessageBox::Save) {
        return saveCurrent();
    }
    return true;
}

void AutomationsPage::loadSelected()
{
    QListWidgetItem *item = m_library->currentItem();
    if (!item) {
        return;
    }
    const QString id = item->data(Qt::UserRole).toString();
    if (id == m_currentId) {
        return;
    }
    if (!confirmDiscard()) {
        m_library->blockSignals(true);
        for (int i = 0; i < m_library->count(); ++i) {
            if (m_library->item(i)->data(Qt::UserRole).toString() == m_currentId) {
                m_library->setCurrentRow(i);
            }
        }
        m_library->blockSignals(false);
        return;
    }
    const WorkflowRow row = WorkflowRepository::load(id);
    QString err;
    Workflow w = Workflow::fromJsonText(row.json, &err);
    w.id = row.id;
    if (w.name.isEmpty()) {
        w.name = row.name;
    }
    m_currentId = id;
    m_editor->setWorkflow(w);
    m_title->setText(tr("Automations — %1").arg(w.name));
    m_dirtyLabel->clear();
}

void AutomationsPage::openWorkflow(const Workflow &workflow, bool markNew)
{
    Workflow w = workflow;
    if (w.id.isEmpty()) {
        w.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    }
    WorkflowRow row;
    row.id = w.id;
    row.name = w.name;
    row.json = w.toJsonText();
    WorkflowRepository::save(row);
    m_currentId = w.id;
    reloadLibrary();
    m_editor->setWorkflow(w);
    m_title->setText(tr("Automations — %1").arg(w.name));
    m_dirtyLabel->setText(markNew ? tr("new — review and Save") : QString());
}

bool AutomationsPage::saveCurrent(bool silent)
{
    Workflow w = m_editor->workflow();
    if (w.id.isEmpty()) {
        w.id = m_currentId.isEmpty() ? QUuid::createUuid().toString(QUuid::WithoutBraces) : m_currentId;
    }
    QString err;
    if (!WorkflowValidator::isValid(w, &err) && !silent) {
        if (QMessageBox::question(this, tr("Workflow has errors"), tr("%1\n\nSave anyway?").arg(err)) != QMessageBox::Yes) {
            return false;
        }
    }
    WorkflowRow row;
    row.id = w.id;
    row.name = w.name;
    row.json = w.toJsonText();
    if (!WorkflowRepository::save(row)) {
        QMessageBox::warning(this, tr("Save"), tr("Could not save the workflow (database unavailable?)."));
        return false;
    }
    m_currentId = w.id;
    m_editor->markClean();
    m_dirtyLabel->setText(tr("saved"));
    reloadLibrary();
    return true;
}

void AutomationsPage::newWorkflow()
{
    if (!confirmDiscard()) {
        return;
    }
    bool ok = false;
    const QString name = QInputDialog::getText(this, tr("New workflow"), tr("Name:"), QLineEdit::Normal, tr("New workflow"), &ok).trimmed();
    if (!ok || name.isEmpty()) {
        return;
    }
    openWorkflow(Workflow::makeEmpty(name), false);
}

void AutomationsPage::duplicateWorkflow()
{
    Workflow w = m_editor->workflow();
    if (w.nodes.isEmpty()) {
        return;
    }
    w.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    w.name += tr(" (copy)");
    openWorkflow(w, true);
}

void AutomationsPage::deleteWorkflow()
{
    if (m_currentId.isEmpty()) {
        return;
    }
    if (QMessageBox::question(this, tr("Delete workflow"), tr("Delete '%1'?").arg(m_editor->workflow().name)) != QMessageBox::Yes) {
        return;
    }
    WorkflowRepository::remove(m_currentId);
    m_currentId.clear();
    m_editor->markClean();
    reloadLibrary();
    if (m_library->count() > 0) {
        m_library->setCurrentRow(0);
    } else {
        m_editor->setWorkflow(Workflow::makeEmpty(tr("Untitled")));
    }
}

void AutomationsPage::importWorkflow()
{
    const QString path = QFileDialog::getOpenFileName(this, tr("Import workflow"), QString(), tr("Workflow JSON (*.json)"));
    if (path.isEmpty()) {
        return;
    }
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        return;
    }
    QString err;
    Workflow w = Workflow::fromJsonText(QString::fromUtf8(f.readAll()), &err);
    if (w.nodes.isEmpty()) {
        QMessageBox::warning(this, tr("Import"), tr("Not a workflow file: %1").arg(err));
        return;
    }
    w.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    // Assets referenced relative to the package folder are resolved next to the JSON.
    const QString assets = QFileInfo(path).absolutePath() + QStringLiteral("/assets/");
    for (WorkflowNode &n : w.nodes) {
        for (auto it = n.params.begin(); it != n.params.end(); ++it) {
            const QString v = it.value().toString();
            if ((it.key() == QLatin1String("image") || it.key() == QLatin1String("apk") || it.key() == QLatin1String("local")) && !v.isEmpty() && QFileInfo(assets + v).exists()) {
                it.value() = assets + v;
            }
        }
    }
    openWorkflow(w, true);
}

void AutomationsPage::exportWorkflow()
{
    const Workflow w = m_editor->workflow();
    const QString path = QFileDialog::getSaveFileName(this, tr("Export workflow"), w.name + QStringLiteral(".json"), tr("Workflow JSON (*.json)"));
    if (path.isEmpty()) {
        return;
    }
    QFile f(path);
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        f.write(w.toJsonText().toUtf8());
    }
}

void AutomationsPage::exportPackage()
{
    // Self-contained folder: workflow.json + assets/ (images, apks) + README.md
    Workflow w = m_editor->workflow();
    const QString dir = QFileDialog::getExistingDirectory(this, tr("Export package into folder"));
    if (dir.isEmpty()) {
        return;
    }
    QString safe = w.name;
    safe.replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9_.\\-]")), QStringLiteral("_"));
    const QString pkg = dir + QLatin1Char('/') + safe;
    QDir().mkpath(pkg + QStringLiteral("/assets"));
    QStringList copied;
    for (WorkflowNode &n : w.nodes) {
        for (auto it = n.params.begin(); it != n.params.end(); ++it) {
            if (it.key() != QLatin1String("image") && it.key() != QLatin1String("apk") && it.key() != QLatin1String("local")) {
                continue;
            }
            const QString src = it.value().toString();
            if (src.isEmpty() || !QFileInfo::exists(src)) {
                continue;
            }
            const QString name = QFileInfo(src).fileName();
            QFile::remove(pkg + QStringLiteral("/assets/") + name);
            if (QFile::copy(src, pkg + QStringLiteral("/assets/") + name)) {
                it.value() = name;    // relative inside the package
                copied << name;
            }
        }
    }
    QFile wf(pkg + QStringLiteral("/workflow.json"));
    if (wf.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        wf.write(w.toJsonText().toUtf8());
    }
    QFile readme(pkg + QStringLiteral("/README.md"));
    if (readme.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        QString text = QStringLiteral("# %1\n\n%2\n\nImport with *Automations → Import JSON…* (workflow.json). Assets referenced by the workflow are in `assets/` and are resolved automatically on import.\n\n## Nodes\n").arg(w.name, w.description);
        for (const WorkflowNode &n : w.nodes) {
            text += QStringLiteral("- %1 (%2)\n").arg(NodeCatalog::spec(n.type).title, n.type);
        }
        if (!copied.isEmpty()) {
            text += QStringLiteral("\n## Assets\n- %1\n").arg(copied.join(QStringLiteral("\n- ")));
        }
        readme.write(text.toUtf8());
    }
    QMessageBox::information(this, tr("Package exported"), tr("Package written to\n%1").arg(QDir::toNativeSeparators(pkg)));
}

void AutomationsPage::generateWithAi()
{
    AiProvider &ai = AiProvider::instance();
    if (!ai.isConfigured()) {
        QMessageBox::information(this, tr("Natural-language assistant"), tr("No AI provider is configured.\n\nThe farm never needs one — but if you want natural-language workflows, install Ollama (https://ollama.com), run `ollama pull llama3.1`, then choose 'Ollama (localhost)' in Settings › Automation."));
        return;
    }
    bool ok = false;
    const QString request = QInputDialog::getMultiLineText(this, tr("Describe the workflow"), tr("Example: Open Settings, go to Display and set brightness to 30%"), QString(), &ok);
    if (!ok || request.trimmed().isEmpty()) {
        return;
    }
    m_dirtyLabel->setText(tr("asking %1…").arg(ai.providerName()));
    ai.generateWorkflow(request, this, [this](Workflow w, QString error, QString raw) {
        if (!error.isEmpty() && w.nodes.isEmpty()) {
            QMessageBox::warning(this, tr("AI"), error + (raw.isEmpty() ? QString() : QStringLiteral("\n\n") + raw.left(800)));
            m_dirtyLabel->clear();
            return;
        }
        const QStringList risky = AiProvider::riskyNodeTitles(w);
        QString msg = tr("The assistant produced '%1' with %2 nodes.").arg(w.name).arg(w.nodes.size());
        if (!error.isEmpty()) {
            msg += QStringLiteral("\n\n") + tr("Validation: %1").arg(error);
        }
        if (!risky.isEmpty()) {
            msg += QStringLiteral("\n\n") + tr("⚠ It contains potentially destructive steps that will require confirmation before running:\n• %1").arg(risky.join(QStringLiteral("\n• ")));
        }
        msg += QStringLiteral("\n\n") + tr("Open it in the editor for review? Nothing runs until you press Run.");
        if (QMessageBox::question(this, tr("AI-generated workflow"), msg) == QMessageBox::Yes) {
            openWorkflow(w, true);
        } else {
            m_dirtyLabel->clear();
        }
    });
}

void AutomationsPage::runCurrent()
{
    if (m_editor->isDirty() && !saveCurrent()) {
        return;
    }
    const Workflow w = m_editor->workflow();
    QString err;
    if (!WorkflowValidator::isValid(w, &err)) {
        QMessageBox::warning(this, tr("Cannot run"), err);
        return;
    }
    emit selectionNeeded();
    QDialog dlg(this);
    dlg.setWindowTitle(tr("Run '%1'").arg(w.name));
    theme::apply(&dlg);
    auto *form = new QFormLayout(&dlg);
    auto *mode = new QComboBox(&dlg);
    mode->addItem(tr("Selected devices (%1)").arg(m_targets.size()), QStringLiteral("selection"));
    mode->addItem(tr("Group…"), QStringLiteral("group"));
    mode->addItem(tr("All online devices (%1)").arg(DeviceRegistry::instance().onlineIds().size()), QStringLiteral("all"));
    auto *group = new QComboBox(&dlg);
    for (const GroupInfo &g : DeviceRegistry::instance().groups()) {
        group->addItem(QStringLiteral("%1 (%2)").arg(g.name).arg(DeviceRegistry::instance().membersOf(g.name).size()), g.name);
    }
    auto *conc = new QSpinBox(&dlg);
    conc->setRange(1, 64);
    conc->setValue(w.runDefaults.value(QStringLiteral("concurrency"), FarmSettings::instance().automationConcurrency()).toInt());
    auto *info = theme::hint(QString(), &dlg);
    form->addRow(tr("Run on"), mode);
    form->addRow(tr("Group"), group);
    form->addRow(tr("Concurrency"), conc);
    form->addRow(info);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    buttons->button(QDialogButtonBox::Ok)->setText(tr("Start"));
    form->addRow(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    auto refreshInfo = [&]() {
        const QStringList t = WorkflowEngine::resolveTargets(mode->currentData().toString(), group->currentData().toString(), m_targets);
        info->setText(tr("%n online device(s) will run this workflow.", nullptr, static_cast<int>(t.size())));
        group->setEnabled(mode->currentData().toString() == QLatin1String("group"));
    };
    connect(mode, &QComboBox::currentIndexChanged, &dlg, [&](int) { refreshInfo(); });
    connect(group, &QComboBox::currentIndexChanged, &dlg, [&](int) { refreshInfo(); });
    refreshInfo();
    if (dlg.exec() != QDialog::Accepted) {
        return;
    }
    const QStringList targets = WorkflowEngine::resolveTargets(mode->currentData().toString(), group->currentData().toString(), m_targets);
    if (targets.isEmpty()) {
        QMessageBox::information(this, tr("Run"), tr("No online target devices."));
        return;
    }
    const QStringList risky = AiProvider::riskyNodeTitles(w);
    if (!risky.isEmpty() || targets.size() >= 10) {
        QString msg = tr("This operation will affect %n device(s).", nullptr, static_cast<int>(targets.size()));
        if (!risky.isEmpty()) {
            msg += QStringLiteral("\n\n") + tr("Destructive steps:\n• %1").arg(risky.join(QStringLiteral("\n• ")));
        }
        if (QMessageBox::warning(this, tr("Confirm run"), msg, QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes) {
            return;
        }
    }
    WorkflowEngine::instance().start(w, targets, conc->value(), QStringLiteral("operator"));
}

void AutomationsPage::reloadRuns()
{
    // runsChanged fires for every device step of every run: update the existing rows
    // in place and only rebuild the list when the set of runs actually changed. The
    // shown run refreshes itself through its own deviceChanged/statusChanged hooks.
    const QList<AutomationRun *> runs = WorkflowEngine::instance().runs();
    const auto label = [](AutomationRun *r) {
        return QStringLiteral("%1  %2\n%3 · %4%").arg(r->startedAt().toString(QStringLiteral("HH:mm:ss")), r->name(), AutomationRun::statusName(r->status())).arg(r->percent());
    };
    const auto color = [](AutomationRun *r) {
        return r->status() == AutomationRun::Failed ? theme::danger() : r->status() == AutomationRun::Running ? theme::success() : theme::text();
    };
    bool sameSet = runs.size() == m_runs->count();
    for (int i = 0; sameSet && i < runs.size(); ++i) {
        sameSet = m_runs->item(i)->data(Qt::UserRole).toString() == runs.at(i)->id();
    }
    if (sameSet) {
        for (int i = 0; i < runs.size(); ++i) {
            QListWidgetItem *item = m_runs->item(i);
            const QString text = label(runs.at(i));
            if (item->text() != text) {
                item->setText(text);
                item->setForeground(color(runs.at(i)));
            }
        }
        return;
    }
    const QString prev = m_runs->currentItem() ? m_runs->currentItem()->data(Qt::UserRole).toString() : QString();
    {
        QSignalBlocker blocker(m_runs);
        m_runs->clear();
        int select = -1;
        int row = 0;
        for (AutomationRun *r : runs) {
            auto *item = new QListWidgetItem(label(r), m_runs);
            item->setData(Qt::UserRole, r->id());
            item->setForeground(color(r));
            if (r->id() == prev) {
                select = row;
            }
            ++row;
        }
        if (select >= 0) {
            m_runs->setCurrentRow(select);    // same run stays shown: no showRun() re-entry
        }
    }
    if (m_shownRun && !runs.contains(m_shownRun.data())) {
        showRun(nullptr);    // the shown run was removed
    }
}

void AutomationsPage::showRun(AutomationRun *run)
{
    if (m_shownRun) {
        disconnect(m_shownRun, nullptr, this, nullptr);
    }
    m_shownRun = run;
    m_runLog->clear();
    if (!run) {
        m_runSummary->setText(tr("Select a run"));
        m_runDevices->setRowCount(0);
        return;
    }
    for (const JobLogRow &l : run->logs()) {
        m_runLog->appendPlainText(QStringLiteral("%1  %2  %3  %4  %5 %6").arg(l.time.toString(QStringLiteral("HH:mm:ss")), l.device.leftJustified(22), l.status.leftJustified(6), l.step, l.message, l.error.isEmpty() ? QString() : QStringLiteral("ERROR: ") + l.error));
    }
    connect(run, &AutomationRun::logAppended, this, [this](const JobLogRow &l) {
        m_runLog->appendPlainText(QStringLiteral("%1  %2  %3  %4  %5 %6").arg(l.time.toString(QStringLiteral("HH:mm:ss")), l.device.leftJustified(22), l.status.leftJustified(6), l.step, l.message, l.error.isEmpty() ? QString() : QStringLiteral("ERROR: ") + l.error));
    });
    connect(run, &AutomationRun::deviceChanged, this, [this](const QString &) { refreshRunDevices(); });
    connect(run, &AutomationRun::statusChanged, this, [this](AutomationRun::Status) { refreshRunDevices(); });
    refreshRunDevices();
}

void AutomationsPage::refreshRunDevices()
{
    if (!m_shownRun) {
        return;
    }
    AutomationRun *r = m_shownRun;
    m_runSummary->setText(QStringLiteral("%1 — %2 [%3] · %4").arg(r->name(), r->summary(), AutomationRun::statusName(r->status()), r->triggeredBy()));
    const QList<AutomationRun::DeviceProgress> all = r->allProgress();
    m_runDevices->setRowCount(static_cast<int>(all.size()));
    for (int i = 0; i < all.size(); ++i) {
        const AutomationRun::DeviceProgress &p = all.at(i);
        const DeviceRecord rec = DeviceRegistry::instance().get(p.id);
        m_runDevices->setItem(i, 0, new QTableWidgetItem(rec.numberString()));
        auto *dev = new QTableWidgetItem(QStringLiteral("%1  %2").arg(rec.displayName(), p.id));
        dev->setData(Qt::UserRole, p.id);
        m_runDevices->setItem(i, 1, dev);
        auto *st = new QTableWidgetItem(p.status);
        st->setForeground(p.status == QLatin1String("ok") ? theme::success() : p.status == QLatin1String("failed") ? theme::danger() : p.status == QLatin1String("running") ? theme::warning() : theme::textMuted());
        m_runDevices->setItem(i, 2, st);
        m_runDevices->setItem(i, 3, new QTableWidgetItem(QString::number(p.steps)));
        m_runDevices->setItem(i, 4, new QTableWidgetItem(p.currentTitle));
        auto *err = new QTableWidgetItem(p.error);
        err->setToolTip(p.errorScreenshot.isEmpty() ? p.error : tr("%1\n\nDouble-click to open the error screenshot").arg(p.error));
        m_runDevices->setItem(i, 5, err);
    }
    const AutomationRun::Status s = r->status();
    m_pauseBtn->setText(s == AutomationRun::Paused ? tr("Resume") : tr("Pause"));
    m_pauseBtn->setEnabled(s == AutomationRun::Running || s == AutomationRun::Paused);
    m_stopBtn->setEnabled(s == AutomationRun::Running || s == AutomationRun::Paused);
    m_retryBtn->setEnabled((s == AutomationRun::Completed || s == AutomationRun::Failed || s == AutomationRun::Cancelled) && !r->failedIds().isEmpty());
    // Highlight the node currently executing on the first running device.
    for (const AutomationRun::DeviceProgress &p : all) {
        if (p.status == QLatin1String("running") && r->workflow().id == m_editor->workflow().id) {
            m_editor->highlightNode(p.currentNode);
            break;
        }
    }
}

} // namespace farm
