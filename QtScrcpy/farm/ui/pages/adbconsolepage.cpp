#include "adbconsolepage.h"

#include <QComboBox>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPointer>
#include <QPushButton>
#include <QRegularExpression>
#include <QSplitter>
#include <QTextCharFormat>
#include <QTextCursor>
#include <QVBoxLayout>

#include "core/farmsettings.h"
#include "devices/devicecommands.h"
#include "devices/deviceregistry.h"
#include "ui/farmtheme.h"

namespace farm {

namespace {
// Split "shell wm size" into adb argv; quoted segments stay together.
QStringList splitCommand(const QString &text)
{
    QStringList out;
    QString cur;
    bool inQuote = false;
    QChar quote;
    for (const QChar c : text) {
        if (inQuote) {
            if (c == quote) {
                inQuote = false;
            } else {
                cur += c;
            }
        } else if (c == QLatin1Char('"') || c == QLatin1Char('\'')) {
            inQuote = true;
            quote = c;
        } else if (c.isSpace()) {
            if (!cur.isEmpty()) {
                out << cur;
                cur.clear();
            }
        } else {
            cur += c;
        }
    }
    if (!cur.isEmpty()) {
        out << cur;
    }
    if (!out.isEmpty() && out.first() == QLatin1String("adb")) {
        out.removeFirst();
    }
    return out;
}
} // namespace

AdbConsolePage::AdbConsolePage(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("page"));
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(20, 16, 20, 16);
    root->setSpacing(10);
    auto *title = new QLabel(tr("ADB Console"), this);
    title->setObjectName(QStringLiteral("pageTitle"));
    root->addWidget(title);

    auto *targets = new QHBoxLayout();
    m_mode = new QComboBox(this);
    m_mode->addItem(tr("Selected devices"), QStringLiteral("selection"));
    m_mode->addItem(tr("Single device"), QStringLiteral("single"));
    m_mode->addItem(tr("Group"), QStringLiteral("group"));
    m_mode->addItem(tr("All online devices"), QStringLiteral("all"));
    m_single = new QComboBox(this);
    m_group = new QComboBox(this);
    m_status = new QLabel(this);
    m_status->setObjectName(QStringLiteral("hint"));
    targets->addWidget(new QLabel(tr("Run on:"), this));
    targets->addWidget(m_mode);
    targets->addWidget(m_single, 1);
    targets->addWidget(m_group, 1);
    targets->addWidget(m_status, 1);
    root->addLayout(targets);

    auto *cmdRow = new QHBoxLayout();
    m_command = new QLineEdit(this);
    m_command->setPlaceholderText(tr("adb command without 'adb', e.g.  shell dumpsys battery   |   reboot   |   shell pm list packages"));
    m_runBtn = theme::button(tr("Run"), this, QStringLiteral("primary"));
    m_cancelBtn = theme::button(tr("Cancel"), this);
    m_cancelBtn->setEnabled(false);
    auto *clearBtn = theme::button(tr("Clear output"), this);
    cmdRow->addWidget(m_command, 1);
    cmdRow->addWidget(m_runBtn);
    cmdRow->addWidget(m_cancelBtn);
    cmdRow->addWidget(clearBtn);
    root->addLayout(cmdRow);

    auto *split = new QSplitter(Qt::Horizontal, this);
    m_output = new QPlainTextEdit(split);
    m_output->setReadOnly(true);
    m_output->setFont(QFont(QStringLiteral("Consolas"), 10));
    m_output->setMaximumBlockCount(20000);
    split->addWidget(m_output);

    auto *side = new QWidget(split);
    auto *sl = new QVBoxLayout(side);
    sl->setContentsMargins(0, 0, 0, 0);
    sl->addWidget(theme::sectionTitle(tr("Saved commands"), side));
    m_savedCategory = new QComboBox(side);
    sl->addWidget(m_savedCategory);
    m_saved = new QListWidget(side);
    sl->addWidget(m_saved, 2);
    auto *presetRow = new QHBoxLayout();
    m_presetName = new QLineEdit(side);
    m_presetName->setPlaceholderText(tr("Preset name"));
    auto *saveBtn = theme::button(tr("Save"), side);
    auto *delBtn = theme::button(tr("Delete"), side, QStringLiteral("danger"));
    presetRow->addWidget(m_presetName, 1);
    presetRow->addWidget(saveBtn);
    presetRow->addWidget(delBtn);
    sl->addLayout(presetRow);
    sl->addWidget(theme::sectionTitle(tr("History"), side));
    m_history = new QListWidget(side);
    sl->addWidget(m_history, 1);
    side->setMinimumWidth(280);
    split->addWidget(side);
    split->setStretchFactor(0, 3);
    split->setStretchFactor(1, 1);
    root->addWidget(split, 1);

    connect(m_mode, &QComboBox::currentIndexChanged, this, [this](int) {
        const QString m = m_mode->currentData().toString();
        m_single->setVisible(m == QLatin1String("single"));
        m_group->setVisible(m == QLatin1String("group"));
        m_status->setText(tr("%n target device(s)", nullptr, static_cast<int>(resolveTargets().size())));
    });
    connect(m_runBtn, &QPushButton::clicked, this, &AdbConsolePage::execute);
    connect(m_command, &QLineEdit::returnPressed, this, &AdbConsolePage::execute);
    connect(clearBtn, &QPushButton::clicked, m_output, &QPlainTextEdit::clear);
    connect(m_saved, &QListWidget::itemClicked, this, [this](QListWidgetItem *it) {
        m_command->setText(it->data(Qt::UserRole).toString());
        m_presetName->setText(it->data(Qt::UserRole + 1).toString());
    });
    connect(m_saved, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem *it) {
        m_command->setText(it->data(Qt::UserRole).toString());
        execute();
    });
    connect(m_savedCategory, &QComboBox::currentIndexChanged, this, [this](int) { reloadSaved(); });
    connect(saveBtn, &QPushButton::clicked, this, &AdbConsolePage::savePreset);
    connect(delBtn, &QPushButton::clicked, this, &AdbConsolePage::deletePreset);
    connect(m_history, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem *it) {
        m_command->setText(it->text());
        execute();
    });
    connect(&DeviceRegistry::instance(), &DeviceRegistry::stateChanged, this, [this](const QString &, DeviceState, DeviceState) { rebuildTargetCombo(); });
    connect(&DeviceRegistry::instance(), &DeviceRegistry::groupsChanged, this, &AdbConsolePage::rebuildTargetCombo);

    m_historyItems = FarmSettings::instance().value(QStringLiteral("console/history")).toStringList();
    for (const QString &h : m_historyItems) {
        m_history->addItem(h);
    }
    rebuildTargetCombo();
    reloadSaved();
    m_single->setVisible(false);
    m_group->setVisible(false);
}

void AdbConsolePage::setTargets(const QStringList &ids)
{
    m_selection = ids;
    m_mode->setCurrentIndex(ids.size() == 1 ? 1 : 0);
    if (ids.size() == 1) {
        m_single->setCurrentIndex(std::max(0, m_single->findData(ids.first())));
    }
    m_status->setText(tr("%n target device(s)", nullptr, static_cast<int>(resolveTargets().size())));
    m_command->setFocus();
}

void AdbConsolePage::rebuildTargetCombo()
{
    const QString prev = m_single->currentData().toString();
    m_single->clear();
    for (const QString &id : DeviceRegistry::instance().sorted(DeviceRegistry::SortKey::Number, true, DeviceRegistry::instance().onlineIds())) {
        const DeviceRecord r = DeviceRegistry::instance().get(id);
        m_single->addItem(QStringLiteral("%1 %2 (%3)").arg(r.numberString(), r.displayName(), id), id);
    }
    m_single->setCurrentIndex(std::max(0, m_single->findData(prev)));
    const QString prevG = m_group->currentText();
    m_group->clear();
    for (const GroupInfo &g : DeviceRegistry::instance().groups()) {
        m_group->addItem(g.name);
    }
    m_group->setCurrentIndex(std::max(0, m_group->findText(prevG)));
}

QStringList AdbConsolePage::resolveTargets() const
{
    const QString m = m_mode->currentData().toString();
    if (m == QLatin1String("single")) {
        return m_single->currentData().toString().isEmpty() ? QStringList() : QStringList{ m_single->currentData().toString() };
    }
    if (m == QLatin1String("group")) {
        QStringList online;
        for (const QString &id : DeviceRegistry::instance().membersOf(m_group->currentText())) {
            if (DeviceRegistry::instance().get(id).isOnline()) {
                online << id;
            }
        }
        return online;
    }
    if (m == QLatin1String("all")) {
        return DeviceRegistry::instance().onlineIds();
    }
    return m_selection;
}

void AdbConsolePage::appendOutput(const QString &text, const QColor &color)
{
    QTextCharFormat fmt;
    fmt.setForeground(color);
    QTextCursor c = m_output->textCursor();
    c.movePosition(QTextCursor::End);
    c.insertText(text + QLatin1Char('\n'), fmt);
    m_output->setTextCursor(c);
}

void AdbConsolePage::execute()
{
    const QString text = m_command->text().trimmed();
    const QStringList ids = resolveTargets();
    if (text.isEmpty()) {
        return;
    }
    if (ids.isEmpty()) {
        appendOutput(tr("No target devices (choose a mode / select devices on the Devices page)."), theme::warning());
        return;
    }
    QStringList args = splitCommand(text);
    if (args.isEmpty()) {
        return;
    }
    // `shell x y z` → single shell string so pipes/quotes reach the device shell intact.
    if (args.first() == QLatin1String("shell") && args.size() > 2) {
        const QString script = text.mid(text.indexOf(QLatin1String("shell")) + 5).trimmed();
        args = QStringList{ QStringLiteral("shell"), script };
    }
    if (!m_historyItems.contains(text)) {
        m_historyItems.prepend(text);
        while (m_historyItems.size() > 100) {
            m_historyItems.removeLast();
        }
        FarmSettings::instance().setValue(QStringLiteral("console/history"), m_historyItems);
        m_history->insertItem(0, text);
    }
    appendOutput(QStringLiteral("$ adb %1   → %2 device(s)").arg(text).arg(ids.size()), theme::accent());
    m_runBtn->setEnabled(false);
    m_cancelBtn->setEnabled(true);
    BatchJob *job = DeviceCommands::adbCommand(ids, args, 60000, tr("Console: %1").arg(text.left(40)));
    QPointer<BatchJob> jobPtr(job);
    connect(m_cancelBtn, &QPushButton::clicked, job, &BatchJob::cancel);
    connect(job, &BatchJob::itemChanged, this, [this, jobPtr](const QString &id) {
        if (!jobPtr) {
            return;
        }
        const BatchJob::Item it = jobPtr->item(id);
        if (it.status == BatchJob::InProgress || it.status == BatchJob::Queued) {
            return;
        }
        const DeviceRecord r = DeviceRegistry::instance().get(id);
        appendOutput(QStringLiteral("── %1 %2 (%3) — %4, %5 ms").arg(r.numberString(), r.displayName(), id, BatchJob::itemStatusName(it.status)).arg(it.durationMs),
                     it.status == BatchJob::Succeeded ? theme::success() : theme::danger());
        if (!it.message.isEmpty()) {
            appendOutput(it.message, theme::text());
        }
    });
    connect(job, &BatchJob::finished, this, [this, jobPtr](BatchJob::Status status) {
        m_runBtn->setEnabled(true);
        m_cancelBtn->setEnabled(false);
        m_cancelBtn->disconnect(this);
        if (jobPtr) {
            appendOutput(QStringLiteral("── %1: %2").arg(BatchJob::statusName(status), jobPtr->summary()), theme::textMuted());
        }
    });
}

void AdbConsolePage::reloadSaved()
{
    m_presets = CommandRepository::loadAll();
    QStringList categories;
    for (const SavedCommand &c : m_presets) {
        if (!categories.contains(c.category)) {
            categories << c.category;
        }
    }
    categories.sort();
    const QString prev = m_savedCategory->currentText();
    m_savedCategory->blockSignals(true);
    m_savedCategory->clear();
    m_savedCategory->addItem(tr("All categories"));
    m_savedCategory->addItems(categories);
    m_savedCategory->setCurrentIndex(std::max(0, m_savedCategory->findText(prev)));
    m_savedCategory->blockSignals(false);
    const QString cat = m_savedCategory->currentIndex() == 0 ? QString() : m_savedCategory->currentText();
    m_saved->clear();
    for (const SavedCommand &c : m_presets) {
        if (!cat.isEmpty() && c.category != cat) {
            continue;
        }
        auto *it = new QListWidgetItem(QStringLiteral("%1  ·  %2").arg(c.name, c.category), m_saved);
        it->setToolTip(QStringLiteral("%1\n%2").arg(c.command, c.description));
        it->setData(Qt::UserRole, c.command);
        it->setData(Qt::UserRole + 1, c.name);
        it->setData(Qt::UserRole + 2, static_cast<qlonglong>(c.id));
    }
}

void AdbConsolePage::savePreset()
{
    const QString cmd = m_command->text().trimmed();
    const QString name = m_presetName->text().trimmed();
    if (cmd.isEmpty() || name.isEmpty()) {
        m_status->setText(tr("Type a command and a preset name."));
        return;
    }
    bool ok = false;
    const QString category = QInputDialog::getItem(this, tr("Save command"), tr("Category:"),
                                                   { tr("Display"), tr("Power"), tr("Network"), tr("Apps"), tr("Diagnostics"), tr("Maintenance"), tr("Custom") }, 6, true, &ok);
    if (!ok) {
        return;
    }
    SavedCommand c;
    for (const SavedCommand &e : m_presets) {
        if (e.name == name) {
            c.id = e.id;
        }
    }
    c.name = name;
    c.command = cmd;
    c.category = category;
    CommandRepository::save(c);
    reloadSaved();
}

void AdbConsolePage::deletePreset()
{
    QListWidgetItem *it = m_saved->currentItem();
    if (!it) {
        return;
    }
    CommandRepository::remove(it->data(Qt::UserRole + 2).toLongLong());
    reloadSaved();
}

} // namespace farm
