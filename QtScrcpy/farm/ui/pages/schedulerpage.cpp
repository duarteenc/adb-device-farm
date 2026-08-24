#include "schedulerpage.h"

#include <QDateTime>
#include <QCheckBox>
#include <QComboBox>
#include <QDateTimeEdit>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QTableWidget>
#include <QTimeEdit>
#include <QUuid>
#include <QVBoxLayout>

#include "automation/workflowengine.h"
#include "devices/deviceregistry.h"
#include "storage/repositories.h"
#include "ui/farmtheme.h"

namespace farm {

SchedulerPage::SchedulerPage(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("page"));
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(20, 16, 20, 16);
    root->setSpacing(10);
    auto *title = new QLabel(tr("Scheduler"), this);
    title->setObjectName(QStringLiteral("pageTitle"));
    root->addWidget(title);
    root->addWidget(theme::hint(tr("Schedules persist in the database and resume when the application starts. A schedule never starts a second run while its previous run is still active."), this));

    auto *body = new QHBoxLayout();
    auto *left = new QVBoxLayout();
    m_table = new QTableWidget(0, 7, this);
    m_table->setHorizontalHeaderLabels({ tr("Name"), tr("Workflow"), tr("When"), tr("Targets"), tr("On"), tr("Next run"), tr("Last run / result") });
    m_table->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setStretchLastSection(true);
    m_table->verticalHeader()->setVisible(false);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    left->addWidget(m_table, 1);
    auto *btns = new QHBoxLayout();
    auto *newBtn = theme::button(tr("New schedule"), this, QStringLiteral("primary"));
    auto *runBtn = theme::button(tr("Run now"), this);
    auto *toggleBtn = theme::button(tr("Enable / disable"), this);
    auto *delBtn = theme::button(tr("Delete"), this, QStringLiteral("danger"));
    btns->addWidget(newBtn);
    btns->addWidget(runBtn);
    btns->addWidget(toggleBtn);
    btns->addWidget(delBtn);
    btns->addStretch(1);
    left->addLayout(btns);
    body->addLayout(left, 1);

    // ---- editor form ----
    auto *card = new QWidget(this);
    card->setObjectName(QStringLiteral("card"));
    card->setFixedWidth(360);
    auto *form = new QFormLayout(card);
    form->setContentsMargins(14, 12, 14, 12);
    form->addRow(theme::sectionTitle(tr("Schedule"), card));
    m_name = new QLineEdit(card);
    m_workflow = new QComboBox(card);
    m_targets = new QComboBox(card);
    m_targets->addItem(tr("All online devices"), QStringLiteral("all"));
    m_targets->addItem(tr("Group"), QStringLiteral("group"));
    m_group = new QComboBox(card);
    m_concurrency = new QSpinBox(card);
    m_concurrency->setRange(1, 64);
    m_concurrency->setValue(5);
    m_kind = new QComboBox(card);
    m_kind->addItem(tr("Run once"), QStringLiteral("once"));
    m_kind->addItem(tr("Every N minutes"), QStringLiteral("interval"));
    m_kind->addItem(tr("Hourly"), QStringLiteral("hourly"));
    m_kind->addItem(tr("Daily"), QStringLiteral("daily"));
    m_kind->addItem(tr("Weekly / specific weekdays"), QStringLiteral("weekly"));
    m_kind->addItem(tr("On application start"), QStringLiteral("appStart"));
    m_kind->addItem(tr("On device event"), QStringLiteral("event"));
    m_onceAt = new QDateTimeEdit(QDateTime::currentDateTime().addSecs(3600), card);
    m_onceAt->setCalendarPopup(true);
    m_onceAt->setDisplayFormat(QStringLiteral("yyyy-MM-dd HH:mm"));
    m_interval = new QSpinBox(card);
    m_interval->setRange(1, 10080);
    m_interval->setValue(60);
    m_interval->setSuffix(tr(" min"));
    m_time = new QTimeEdit(QTime(9, 0), card);
    m_time->setDisplayFormat(QStringLiteral("HH:mm"));
    m_weekdaysRow = new QWidget(card);
    auto *wl = new QHBoxLayout(m_weekdaysRow);
    wl->setContentsMargins(0, 0, 0, 0);
    const char *days[] = { "Mo", "Tu", "We", "Th", "Fr", "Sa", "Su" };
    for (int i = 0; i < 7; ++i) {
        auto *c = new QCheckBox(QLatin1String(days[i]), m_weekdaysRow);
        c->setChecked(i < 5);
        m_weekdays << c;
        wl->addWidget(c);
    }
    m_eventType = new QComboBox(card);
    m_eventType->addItem(tr("Device connected"), QStringLiteral("deviceConnected"));
    m_eventType->addItem(tr("Device disconnected"), QStringLiteral("deviceDisconnected"));
    m_eventType->addItem(tr("Device reconnected"), QStringLiteral("deviceReconnected"));
    m_eventType->addItem(tr("Battery below X %"), QStringLiteral("batteryBelow"));
    m_eventType->addItem(tr("Battery above X %"), QStringLiteral("batteryAbove"));
    m_eventType->addItem(tr("Temperature above X °C"), QStringLiteral("temperatureAbove"));
    m_threshold = new QDoubleSpinBox(card);
    m_threshold->setRange(0, 100);
    m_threshold->setValue(20);
    m_cooldown = new QSpinBox(card);
    m_cooldown->setRange(0, 86400);
    m_cooldown->setValue(300);
    m_cooldown->setSuffix(tr(" s"));
    m_missed = new QComboBox(card);
    m_missed->addItem(tr("Skip missed run"), QStringLiteral("skip"));
    m_missed->addItem(tr("Run immediately"), QStringLiteral("immediate"));
    m_missed->addItem(tr("Run once at next startup"), QStringLiteral("nextStart"));
    m_enabled = new QCheckBox(tr("Enabled"), card);
    m_enabled->setChecked(true);
    m_preview = theme::hint(QString(), card);
    form->addRow(tr("Name"), m_name);
    form->addRow(tr("Workflow"), m_workflow);
    form->addRow(tr("Targets"), m_targets);
    form->addRow(tr("Group"), m_group);
    form->addRow(tr("Concurrency"), m_concurrency);
    form->addRow(tr("Trigger"), m_kind);
    form->addRow(tr("At"), m_onceAt);
    form->addRow(tr("Interval"), m_interval);
    form->addRow(tr("Time"), m_time);
    form->addRow(tr("Days"), m_weekdaysRow);
    form->addRow(tr("Event"), m_eventType);
    form->addRow(tr("Threshold"), m_threshold);
    form->addRow(tr("Per-device cooldown"), m_cooldown);
    form->addRow(tr("Missed run"), m_missed);
    form->addRow(QString(), m_enabled);
    form->addRow(m_preview);
    auto *saveBtn = theme::button(tr("Save schedule"), card, QStringLiteral("primary"));
    form->addRow(saveBtn);
    body->addWidget(card);
    root->addLayout(body, 1);

    // ---- wiring ----
    auto refreshCombos = [this]() {
        const QString prevW = m_workflow->currentData().toString();
        m_workflow->clear();
        for (const WorkflowRow &w : WorkflowRepository::loadAll()) {
            m_workflow->addItem(w.name, w.id);
        }
        m_workflow->setCurrentIndex(std::max(0, m_workflow->findData(prevW)));
        const QString prevG = m_group->currentText();
        m_group->clear();
        for (const GroupInfo &g : DeviceRegistry::instance().groups()) {
            m_group->addItem(g.name);
        }
        m_group->setCurrentIndex(std::max(0, m_group->findText(prevG)));
    };
    refreshCombos();
    connect(&DeviceRegistry::instance(), &DeviceRegistry::groupsChanged, this, refreshCombos);
    connect(m_kind, &QComboBox::currentIndexChanged, this, [this](int) { updateFormVisibility(); });
    connect(m_targets, &QComboBox::currentIndexChanged, this, [this](int) { updateFormVisibility(); });
    for (QWidget *w : QList<QWidget *>{ m_onceAt, m_interval, m_time, m_eventType, m_threshold }) {
        Q_UNUSED(w);
    }
    auto preview = [this]() {
        const Schedule s = formToSchedule();
        const QDateTime next = Scheduler::computeNextRun(s, QDateTime::currentDateTime());
        m_preview->setText(s.kind == QLatin1String("event") || s.kind == QLatin1String("appStart") ? s.describe() : tr("%1 — next: %2").arg(s.describe(), next.isValid() ? next.toString(QStringLiteral("ddd yyyy-MM-dd HH:mm")) : tr("never")));
    };
    connect(m_kind, &QComboBox::currentIndexChanged, this, [preview](int) { preview(); });
    connect(m_onceAt, &QDateTimeEdit::dateTimeChanged, this, [preview](const QDateTime &) { preview(); });
    connect(m_interval, &QSpinBox::valueChanged, this, [preview](int) { preview(); });
    connect(m_time, &QTimeEdit::timeChanged, this, [preview](const QTime &) { preview(); });
    for (QCheckBox *c : m_weekdays) {
        connect(c, &QCheckBox::toggled, this, [preview](bool) { preview(); });
    }
    connect(saveBtn, &QPushButton::clicked, this, [this, refreshCombos]() {
        Schedule s = formToSchedule();
        if (s.name.isEmpty()) {
            s.name = m_workflow->currentText() + QStringLiteral(" — ") + s.describe();
        }
        if (s.workflowId.isEmpty()) {
            QMessageBox::warning(this, tr("Schedule"), tr("Create a workflow first (Automations page)."));
            return;
        }
        if (s.id.isEmpty()) {
            s.id = QUuid::createUuid().toString(QUuid::WithoutBraces);    // save() takes a copy: the id must exist here
        }
        m_editingId = s.id;    // before save(): the schedulesChanged -> reload() selects the new row
        Scheduler::instance().save(s);
        refreshCombos();
        reload();
    });
    connect(newBtn, &QPushButton::clicked, this, [this]() {
        m_table->setCurrentItem(nullptr);    // clears the current index too, so the handler below sees no row
        m_editingId.clear();
        loadIntoForm(Schedule());
    });
    connect(m_table, &QTableWidget::itemSelectionChanged, this, [this]() {
        const int row = m_table->currentRow();
        if (row < 0 || m_table->selectedItems().isEmpty() || !m_table->item(row, 0)) {
            return;    // a deselect must never reload the old schedule into the form
        }
        const QString id = m_table->item(row, 0)->data(Qt::UserRole).toString();
        m_editingId = id;
        loadIntoForm(Scheduler::instance().schedule(id));
    });
    connect(runBtn, &QPushButton::clicked, this, [this]() {
        if (!m_editingId.isEmpty()) {
            Scheduler::instance().runNow(m_editingId);
        }
    });
    connect(toggleBtn, &QPushButton::clicked, this, [this]() {
        if (!m_editingId.isEmpty()) {
            Scheduler::instance().setEnabled(m_editingId, !Scheduler::instance().schedule(m_editingId).enabled);
        }
    });
    connect(delBtn, &QPushButton::clicked, this, [this]() {
        if (m_editingId.isEmpty()) {
            return;
        }
        if (QMessageBox::question(this, tr("Delete schedule"), tr("Delete this schedule?")) == QMessageBox::Yes) {
            Scheduler::instance().remove(m_editingId);
            m_editingId.clear();
        }
    });
    connect(&Scheduler::instance(), &Scheduler::schedulesChanged, this, &SchedulerPage::reload);
    updateFormVisibility();
    preview();
    reload();
}

void SchedulerPage::updateFormVisibility()
{
    const QString k = m_kind->currentData().toString();
    m_onceAt->setVisible(k == QLatin1String("once"));
    m_interval->setVisible(k == QLatin1String("interval"));
    m_time->setVisible(k == QLatin1String("hourly") || k == QLatin1String("daily") || k == QLatin1String("weekly"));
    m_weekdaysRow->setVisible(k == QLatin1String("weekly"));
    m_eventType->setVisible(k == QLatin1String("event"));
    m_threshold->setVisible(k == QLatin1String("event"));
    m_cooldown->setVisible(k == QLatin1String("event"));
    m_missed->setVisible(k != QLatin1String("event") && k != QLatin1String("appStart"));
    m_group->setEnabled(m_targets->currentData().toString() == QLatin1String("group"));
}

Schedule SchedulerPage::formToSchedule() const
{
    Schedule s = m_editingId.isEmpty() ? Schedule() : Scheduler::instance().schedule(m_editingId);
    s.name = m_name->text().trimmed();
    s.workflowId = m_workflow->currentData().toString();
    s.targetsMode = m_targets->currentData().toString();
    s.group = m_group->currentText();
    s.concurrency = m_concurrency->value();
    s.kind = m_kind->currentData().toString();
    s.onceAt = m_onceAt->dateTime();
    s.intervalMinutes = m_interval->value();
    s.timeOfDay = m_time->time();
    s.weekdays.clear();
    for (int i = 0; i < 7; ++i) {
        if (m_weekdays.at(i)->isChecked()) {
            s.weekdays << (i + 1);
        }
    }
    s.eventType = m_eventType->currentData().toString();
    s.eventThreshold = m_threshold->value();
    s.eventCooldownSeconds = m_cooldown->value();
    s.missedPolicy = m_missed->currentData().toString();
    s.enabled = m_enabled->isChecked();
    return s;
}

void SchedulerPage::loadIntoForm(const Schedule &s)
{
    m_name->setText(s.name);
    m_workflow->setCurrentIndex(std::max(0, m_workflow->findData(s.workflowId)));
    m_targets->setCurrentIndex(std::max(0, m_targets->findData(s.targetsMode)));
    m_group->setCurrentIndex(std::max(0, m_group->findText(s.group)));
    m_concurrency->setValue(s.concurrency);
    m_kind->setCurrentIndex(std::max(0, m_kind->findData(s.kind)));
    if (s.onceAt.isValid()) {
        m_onceAt->setDateTime(s.onceAt);
    }
    m_interval->setValue(s.intervalMinutes);
    m_time->setTime(s.timeOfDay);
    for (int i = 0; i < 7; ++i) {
        m_weekdays.at(i)->setChecked(s.weekdays.contains(i + 1));
    }
    m_eventType->setCurrentIndex(std::max(0, m_eventType->findData(s.eventType)));
    m_threshold->setValue(s.eventThreshold);
    m_cooldown->setValue(s.eventCooldownSeconds);
    m_missed->setCurrentIndex(std::max(0, m_missed->findData(s.missedPolicy)));
    m_enabled->setChecked(s.enabled);
    updateFormVisibility();
}

void SchedulerPage::reload()
{
    const QList<Schedule> list = Scheduler::instance().schedules();
    m_table->setRowCount(static_cast<int>(list.size()));
    for (int i = 0; i < list.size(); ++i) {
        const Schedule &s = list.at(i);
        auto *name = new QTableWidgetItem(s.name);
        name->setData(Qt::UserRole, s.id);
        m_table->setItem(i, 0, name);
        bool found = false;
        const Workflow w = WorkflowEngine::loadWorkflow(s.workflowId, &found);
        m_table->setItem(i, 1, new QTableWidgetItem(found ? w.name : tr("(missing) %1").arg(s.workflowId)));
        m_table->setItem(i, 2, new QTableWidgetItem(s.describe()));
        m_table->setItem(i, 3, new QTableWidgetItem(s.targetsMode == QLatin1String("group") ? s.group : tr("all online")));
        auto *on = new QTableWidgetItem(s.enabled ? tr("yes") : tr("no"));
        on->setForeground(s.enabled ? theme::success() : theme::textMuted());
        m_table->setItem(i, 4, on);
        m_table->setItem(i, 5, new QTableWidgetItem(s.nextRun.isValid() ? s.nextRun.toString(QStringLiteral("ddd dd HH:mm")) : QString()));
        m_table->setItem(i, 6, new QTableWidgetItem((s.lastRun.isValid() ? s.lastRun.toString(QStringLiteral("dd HH:mm")) + QStringLiteral(" — ") : QString()) + s.lastResult));
        if (s.id == m_editingId) {
            m_table->selectRow(i);
        }
    }
}

} // namespace farm
