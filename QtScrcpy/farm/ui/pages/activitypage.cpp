#include "activitypage.h"

#include <QDesktopServices>
#include <QUrl>
#include <QCheckBox>
#include <QComboBox>
#include <QFile>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

#include "core/farmlog.h"
#include "ui/farmtheme.h"

namespace farm {

ActivityPage::ActivityPage(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("page"));
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(20, 16, 20, 16);
    root->setSpacing(10);
    auto *title = new QLabel(tr("Activity"), this);
    title->setObjectName(QStringLiteral("pageTitle"));
    root->addWidget(title);

    auto *filters = new QHBoxLayout();
    m_level = new QComboBox(this);
    m_level->addItem(tr("All levels"), -1);
    m_level->addItem(tr("Info"), ActivityEntry::Info);
    m_level->addItem(tr("Warning"), ActivityEntry::Warning);
    m_level->addItem(tr("Error"), ActivityEntry::Error);
    m_category = new QComboBox(this);
    m_category->addItem(tr("All categories"), -1);
    for (int c = ActivityEntry::System; c <= ActivityEntry::App; ++c) {
        m_category->addItem(ActivityEntry::categoryName(static_cast<ActivityEntry::Category>(c)), c);
    }
    m_device = new QLineEdit(this);
    m_device->setPlaceholderText(tr("Device filter (id / ip)"));
    m_device->setClearButtonEnabled(true);
    m_autoScroll = new QCheckBox(tr("Follow"), this);
    m_autoScroll->setChecked(true);
    auto *exportBtn = theme::button(tr("Export…"), this);
    auto *clearBtn = theme::button(tr("Clear"), this);
    auto *openLog = theme::button(tr("Open log folder"), this);
    filters->addWidget(m_level);
    filters->addWidget(m_category);
    filters->addWidget(m_device, 1);
    filters->addWidget(m_autoScroll);
    filters->addWidget(exportBtn);
    filters->addWidget(clearBtn);
    filters->addWidget(openLog);
    root->addLayout(filters);

    m_table = new QTableWidget(0, 5, this);
    m_table->setHorizontalHeaderLabels({ tr("Time"), tr("Level"), tr("Category"), tr("Device"), tr("Message") });
    m_table->horizontalHeader()->setSectionResizeMode(4, QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    m_table->verticalHeader()->setVisible(false);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setAlternatingRowColors(true);
    root->addWidget(m_table, 1);

    connect(m_level, &QComboBox::currentIndexChanged, this, [this](int) { rebuild(); });
    connect(m_category, &QComboBox::currentIndexChanged, this, [this](int) { rebuild(); });
    connect(m_device, &QLineEdit::textChanged, this, [this](const QString &) { rebuild(); });
    connect(clearBtn, &QPushButton::clicked, this, []() { ActivityLog::instance().clear(); });
    connect(openLog, &QPushButton::clicked, this, []() { QDesktopServices::openUrl(QUrl::fromLocalFile(FarmLog::instance().directory())); });
    connect(exportBtn, &QPushButton::clicked, this, [this]() {
        const QString path = QFileDialog::getSaveFileName(this, tr("Export activity"), QStringLiteral("activity.txt"), tr("Text files (*.txt)"));
        if (path.isEmpty()) {
            return;
        }
        QFile f(path);
        if (f.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
            for (const ActivityEntry &e : ActivityLog::instance().entries()) {
                if (passes(e)) {
                    f.write(QStringLiteral("%1\t%2\t%3\t%4\t%5\n").arg(e.time.toString(Qt::ISODate), ActivityEntry::levelName(e.level), ActivityEntry::categoryName(e.category), e.device, e.message).toUtf8());
                }
            }
        }
    });
    connect(m_table, &QTableWidget::itemDoubleClicked, this, [this](QTableWidgetItem *it) {
        const QString id = m_table->item(it->row(), 3)->text();
        if (!id.isEmpty()) {
            emit deviceActivated(id);
        }
    });
    connect(&ActivityLog::instance(), &ActivityLog::entryAdded, this, [this](const ActivityEntry &e) {
        if (passes(e)) {
            appendRow(e);
            if (m_autoScroll->isChecked()) {
                m_table->scrollToBottom();
            }
        }
    });
    connect(&ActivityLog::instance(), &ActivityLog::cleared, this, &ActivityPage::rebuild);
    rebuild();
}

bool ActivityPage::passes(const ActivityEntry &e) const
{
    const int level = m_level->currentData().toInt();
    if (level >= 0 && static_cast<int>(e.level) != level) {
        return false;
    }
    const int cat = m_category->currentData().toInt();
    if (cat >= 0 && static_cast<int>(e.category) != cat) {
        return false;
    }
    const QString dev = m_device->text().trimmed();
    if (!dev.isEmpty() && !e.device.contains(dev, Qt::CaseInsensitive)) {
        return false;
    }
    return true;
}

void ActivityPage::appendRow(const ActivityEntry &e)
{
    const int row = m_table->rowCount();
    m_table->insertRow(row);
    m_table->setItem(row, 0, new QTableWidgetItem(e.time.toString(QStringLiteral("HH:mm:ss"))));
    auto *lvl = new QTableWidgetItem(ActivityEntry::levelName(e.level));
    lvl->setForeground(e.level == ActivityEntry::Error ? theme::danger() : e.level == ActivityEntry::Warning ? theme::warning() : theme::textMuted());
    m_table->setItem(row, 1, lvl);
    m_table->setItem(row, 2, new QTableWidgetItem(ActivityEntry::categoryName(e.category)));
    m_table->setItem(row, 3, new QTableWidgetItem(e.device));
    m_table->setItem(row, 4, new QTableWidgetItem(e.message));
}

void ActivityPage::rebuild()
{
    m_table->setRowCount(0);
    for (const ActivityEntry &e : ActivityLog::instance().entries()) {
        if (passes(e)) {
            appendRow(e);
        }
    }
    m_table->scrollToBottom();
}

} // namespace farm
