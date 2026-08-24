#include "groupspage.h"

#include <QDragMoveEvent>
#include <QIcon>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPixmap>
#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDrag>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QMimeData>
#include <QPushButton>
#include <QSpinBox>
#include <QTableWidget>
#include <QVBoxLayout>

#include "devices/deviceregistry.h"
#include "devices/keepawakemanager.h"
#include "ui/farmtheme.h"

namespace farm {

namespace {
const char *kMime = "application/x-farm-device-ids";
}

GroupsPage::GroupsPage(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("page"));
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(20, 16, 20, 16);
    root->setSpacing(12);
    auto *title = new QLabel(tr("Groups"), this);
    title->setObjectName(QStringLiteral("pageTitle"));
    root->addWidget(title);
    root->addWidget(theme::hint(tr("Drag devices from the tables onto a group to move them. Double-click a group to show only its devices on the Devices page."), this));

    auto *body = new QHBoxLayout();
    body->setSpacing(12);

    // ---- left: groups ----
    auto *left = new QVBoxLayout();
    m_groups = new QListWidget(this);
    m_groups->setAcceptDrops(true);
    m_groups->setDragDropMode(QAbstractItemView::DropOnly);
    m_groups->viewport()->installEventFilter(this);
    m_groups->setMinimumWidth(240);
    left->addWidget(m_groups, 1);
    auto *btns = new QHBoxLayout();
    auto *addBtn = theme::button(tr("New"), this, QStringLiteral("primary"));
    auto *renameBtn = theme::button(tr("Rename"), this);
    auto *colorBtn = theme::button(tr("Colour"), this);
    auto *delBtn = theme::button(tr("Delete"), this, QStringLiteral("danger"));
    btns->addWidget(addBtn);
    btns->addWidget(renameBtn);
    btns->addWidget(colorBtn);
    btns->addWidget(delBtn);
    left->addLayout(btns);
    auto *showBtn = theme::button(tr("Show on Devices page"), this);
    auto *selectBtn = theme::button(tr("Select members on Devices page"), this);
    left->addWidget(showBtn);
    left->addWidget(selectBtn);

    // group settings
    auto *settingsCard = new QWidget(this);
    settingsCard->setObjectName(QStringLiteral("card"));
    auto *sl = new QVBoxLayout(settingsCard);
    sl->setContentsMargins(12, 8, 12, 10);
    sl->addWidget(theme::sectionTitle(tr("Group settings"), settingsCard));
    m_keepAwake = new QComboBox(settingsCard);
    m_keepAwake->addItem(tr("Keep awake: inherit global"), -1);
    m_keepAwake->addItem(tr("Keep awake: ON"), 1);
    m_keepAwake->addItem(tr("Keep awake: OFF"), 0);
    m_preset = new QComboBox(settingsCard);
    m_preset->addItem(tr("Quality: inherit global"), QString());
    m_preset->addItem(tr("Quality: performance"), QStringLiteral("performance"));
    m_preset->addItem(tr("Quality: balanced"), QStringLiteral("balanced"));
    m_preset->addItem(tr("Quality: quality"), QStringLiteral("quality"));
    auto spin = [&](const QString &label, int lo, int hi, int step) {
        auto *row = new QHBoxLayout();
        row->addWidget(new QLabel(label, settingsCard));
        auto *s = new QSpinBox(settingsCard);
        s->setRange(lo, hi);
        s->setSingleStep(step);
        s->setSpecialValueText(tr("inherit"));
        row->addWidget(s);
        sl->addLayout(row);
        return s;
    };
    sl->addWidget(m_keepAwake);
    sl->addWidget(m_preset);
    m_maxSize = spin(tr("Max size (px)"), 0, 2160, 40);
    m_fps = spin(tr("FPS"), 0, 120, 5);
    m_bitrate = spin(tr("Bitrate (Mbps)"), 0, 50, 1);
    left->addWidget(settingsCard);
    body->addLayout(left, 0);

    // ---- right: members / unassigned ----
    auto *right = new QVBoxLayout();
    m_title = new QLabel(this);
    m_title->setStyleSheet(QStringLiteral("font-weight:bold; font-size:14px;"));
    right->addWidget(m_title);
    auto makeTable = [this]() {
        auto *t = new QTableWidget(0, 5, this);
        t->setHorizontalHeaderLabels({ tr("#"), tr("Name"), tr("ID"), tr("State"), tr("Model") });
        t->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
        t->horizontalHeader()->setStretchLastSection(true);
        t->verticalHeader()->setVisible(false);
        t->setSelectionBehavior(QAbstractItemView::SelectRows);
        t->setSelectionMode(QAbstractItemView::ExtendedSelection);
        t->setEditTriggers(QAbstractItemView::NoEditTriggers);
        t->setDragEnabled(true);
        t->setDragDropMode(QAbstractItemView::DragOnly);
        t->viewport()->installEventFilter(this);
        return t;
    };
    m_members = makeTable();
    right->addWidget(m_members, 2);
    auto *moveRow = new QHBoxLayout();
    auto *removeBtn = theme::button(tr("Remove selected from group"), this);
    auto *moveBtn = theme::button(tr("Move selected to…"), this);
    moveRow->addWidget(removeBtn);
    moveRow->addWidget(moveBtn);
    moveRow->addStretch(1);
    right->addLayout(moveRow);
    right->addWidget(theme::sectionTitle(tr("Devices without a group (select and drag, or use Add)"), this));
    m_unassigned = makeTable();
    right->addWidget(m_unassigned, 1);
    auto *addToGroup = theme::button(tr("Add selected to current group"), this, QStringLiteral("primary"));
    right->addWidget(addToGroup);
    body->addLayout(right, 1);
    root->addLayout(body, 1);

    // ---- behaviour ----
    connect(addBtn, &QPushButton::clicked, this, [this]() {
        bool ok = false;
        const QString name = QInputDialog::getText(this, tr("New group"), tr("Group name:"), QLineEdit::Normal, QString(), &ok).trimmed();
        if (ok && !name.isEmpty()) {
            if (!DeviceRegistry::instance().createGroup(name)) {
                QMessageBox::warning(this, tr("Group"), tr("A group with that name already exists."));
            }
        }
    });
    connect(renameBtn, &QPushButton::clicked, this, [this]() {
        const QString g = currentGroup();
        if (g.isEmpty()) {
            return;
        }
        bool ok = false;
        const QString name = QInputDialog::getText(this, tr("Rename group"), tr("New name:"), QLineEdit::Normal, g, &ok).trimmed();
        if (ok && !name.isEmpty()) {
            DeviceRegistry::instance().renameGroup(g, name);
        }
    });
    connect(colorBtn, &QPushButton::clicked, this, [this]() {
        const QString g = currentGroup();
        if (g.isEmpty()) {
            return;
        }
        const QColor c = QColorDialog::getColor(QColor(DeviceRegistry::instance().group(g).color), this, tr("Group colour"));
        if (c.isValid()) {
            DeviceRegistry::instance().setGroupColor(g, c.name());
        }
    });
    connect(delBtn, &QPushButton::clicked, this, [this]() {
        const QString g = currentGroup();
        if (g.isEmpty()) {
            return;
        }
        const int members = static_cast<int>(DeviceRegistry::instance().membersOf(g).size());
        if (QMessageBox::question(this, tr("Delete group"), tr("Delete group '%1'? Its %2 device(s) become ungrouped (nothing is removed).").arg(g).arg(members)) == QMessageBox::Yes) {
            DeviceRegistry::instance().deleteGroup(g);
        }
    });
    connect(showBtn, &QPushButton::clicked, this, [this]() { emit showGroupRequested(currentGroup()); });
    connect(selectBtn, &QPushButton::clicked, this, [this]() { emit selectGroupRequested(currentGroup()); });
    connect(m_groups, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem *) { emit showGroupRequested(currentGroup()); });
    connect(m_groups, &QListWidget::currentRowChanged, this, [this](int) { reloadMembers(); });
    connect(removeBtn, &QPushButton::clicked, this, [this]() { DeviceRegistry::instance().assignGroup(selectedMemberIds(), QString()); });
    connect(moveBtn, &QPushButton::clicked, this, [this, moveBtn]() {
        const QStringList ids = selectedMemberIds();
        if (ids.isEmpty()) {
            return;
        }
        QMenu menu(this);
        for (const GroupInfo &g : DeviceRegistry::instance().groups()) {
            const QString name = g.name;
            menu.addAction(name, this, [ids, name]() { DeviceRegistry::instance().assignGroup(ids, name); });
        }
        menu.exec(moveBtn->mapToGlobal(QPoint(0, moveBtn->height())));
    });
    connect(addToGroup, &QPushButton::clicked, this, [this]() {
        const QString g = currentGroup();
        QStringList ids;
        const QList<QTableWidgetSelectionRange> ranges = m_unassigned->selectedRanges();
        for (const QTableWidgetSelectionRange &r : ranges) {
            for (int row = r.topRow(); row <= r.bottomRow(); ++row) {
                ids << m_unassigned->item(row, 2)->text();
            }
        }
        if (!g.isEmpty() && !ids.isEmpty()) {
            DeviceRegistry::instance().assignGroup(ids, g);
        }
    });
    auto settingsChanged = [this]() {
        if (!m_loadingSettings) {
            saveGroupSettings();
        }
    };
    connect(m_keepAwake, &QComboBox::currentIndexChanged, this, [settingsChanged](int) { settingsChanged(); });
    connect(m_preset, &QComboBox::currentIndexChanged, this, [settingsChanged](int) { settingsChanged(); });
    connect(m_maxSize, &QSpinBox::valueChanged, this, [settingsChanged](int) { settingsChanged(); });
    connect(m_fps, &QSpinBox::valueChanged, this, [settingsChanged](int) { settingsChanged(); });
    connect(m_bitrate, &QSpinBox::valueChanged, this, [settingsChanged](int) { settingsChanged(); });

    connect(&DeviceRegistry::instance(), &DeviceRegistry::groupsChanged, this, &GroupsPage::reloadGroups);
    connect(&DeviceRegistry::instance(), &DeviceRegistry::deviceChanged, this, [this](const QString &) { reloadMembers(); });
    connect(&DeviceRegistry::instance(), &DeviceRegistry::deviceAdded, this, [this](const QString &) { reloadMembers(); });
    connect(&DeviceRegistry::instance(), &DeviceRegistry::deviceRemoved, this, [this](const QString &) { reloadMembers(); });
    reloadGroups();
}

QString GroupsPage::currentGroup() const
{
    QListWidgetItem *it = m_groups->currentItem();
    return it ? it->data(Qt::UserRole).toString() : QString();
}

QStringList GroupsPage::selectedMemberIds() const
{
    QStringList ids;
    const QList<QTableWidgetSelectionRange> ranges = m_members->selectedRanges();
    for (const QTableWidgetSelectionRange &r : ranges) {
        for (int row = r.topRow(); row <= r.bottomRow(); ++row) {
            ids << m_members->item(row, 2)->text();
        }
    }
    return ids;
}

void GroupsPage::reloadGroups()
{
    const QString previous = currentGroup();
    m_groups->clear();
    int row = 0;
    int select = -1;
    for (const GroupInfo &g : DeviceRegistry::instance().groups()) {
        const int n = static_cast<int>(DeviceRegistry::instance().membersOf(g.name).size());
        auto *item = new QListWidgetItem(QStringLiteral("%1   (%2)").arg(g.name).arg(n), m_groups);
        item->setData(Qt::UserRole, g.name);
        QPixmap pm(14, 14);
        pm.fill(QColor(g.color));
        item->setIcon(QIcon(pm));
        if (g.name == previous) {
            select = row;
        }
        ++row;
    }
    if (m_groups->count() > 0) {
        m_groups->setCurrentRow(select >= 0 ? select : 0);
    }
    reloadMembers();
}

void GroupsPage::reloadMembers()
{
    const QString g = currentGroup();
    m_title->setText(g.isEmpty() ? tr("No group selected") : tr("Group '%1'").arg(g));
    auto fill = [](QTableWidget *table, const QStringList &ids) {
        table->setRowCount(static_cast<int>(ids.size()));
        for (int i = 0; i < ids.size(); ++i) {
            const DeviceRecord r = DeviceRegistry::instance().get(ids.at(i));
            table->setItem(i, 0, new QTableWidgetItem(r.numberString()));
            table->setItem(i, 1, new QTableWidgetItem(r.displayName()));
            table->setItem(i, 2, new QTableWidgetItem(r.id));
            auto *st = new QTableWidgetItem(deviceStateName(r.state));
            st->setForeground(theme::stateColor(static_cast<int>(r.state)));
            table->setItem(i, 3, st);
            table->setItem(i, 4, new QTableWidgetItem(r.model));
        }
    };
    fill(m_members, g.isEmpty() ? QStringList() : DeviceRegistry::instance().membersOf(g));
    QStringList unassigned;
    for (const DeviceRecord &r : DeviceRegistry::instance().all()) {
        if (r.group.isEmpty()) {
            unassigned << r.id;
        }
    }
    fill(m_unassigned, DeviceRegistry::instance().sorted(DeviceRegistry::SortKey::Number, true, unassigned));

    m_loadingSettings = true;
    const GroupInfo info = DeviceRegistry::instance().group(g);
    m_keepAwake->setCurrentIndex(std::max(0, m_keepAwake->findData(info.settings.value(QStringLiteral("keepAwake"), -1).toInt())));
    m_preset->setCurrentIndex(std::max(0, m_preset->findData(info.settings.value(QStringLiteral("preset")).toString())));
    m_maxSize->setValue(info.settings.value(QStringLiteral("maxSize")).toInt());
    m_fps->setValue(info.settings.value(QStringLiteral("fps")).toInt());
    m_bitrate->setValue(info.settings.value(QStringLiteral("bitRate")).toInt() / 1000000);
    m_loadingSettings = false;
}

void GroupsPage::saveGroupSettings()
{
    const QString g = currentGroup();
    if (g.isEmpty()) {
        return;
    }
    QVariantMap s;
    s[QStringLiteral("keepAwake")] = m_keepAwake->currentData().toInt();
    s[QStringLiteral("preset")] = m_preset->currentData().toString();
    s[QStringLiteral("maxSize")] = m_maxSize->value();
    s[QStringLiteral("fps")] = m_fps->value();
    s[QStringLiteral("bitRate")] = m_bitrate->value() * 1000000;
    DeviceRegistry::instance().setGroupSettings(g, s);
    if (m_keepAwake->currentData().toInt() >= 0) {
        KeepAwakeManager::instance().applyPolicy(DeviceRegistry::instance().membersOf(g));
    }
}

bool GroupsPage::eventFilter(QObject *watched, QEvent *event)
{
    // Drag from either table (ids in a private mime type), drop onto a group row.
    if ((watched == m_members->viewport() || watched == m_unassigned->viewport()) && event->type() == QEvent::MouseMove) {
        auto *me = static_cast<QMouseEvent *>(event);
        if (me->buttons() & Qt::LeftButton) {
            QTableWidget *table = watched == m_members->viewport() ? m_members : m_unassigned;
            QStringList ids;
            const QList<QTableWidgetSelectionRange> ranges = table->selectedRanges();
            for (const QTableWidgetSelectionRange &r : ranges) {
                for (int row = r.topRow(); row <= r.bottomRow(); ++row) {
                    ids << table->item(row, 2)->text();
                }
            }
            if (!ids.isEmpty()) {
                auto *drag = new QDrag(table);
                auto *mime = new QMimeData;
                mime->setData(QLatin1String(kMime), ids.join(QLatin1Char('\n')).toUtf8());
                mime->setText(ids.join(QLatin1Char('\n')));
                drag->setMimeData(mime);
                drag->exec(Qt::MoveAction);
                return true;
            }
        }
    }
    if (watched == m_groups->viewport()) {
        if (event->type() == QEvent::DragEnter || event->type() == QEvent::DragMove) {
            auto *de = static_cast<QDragMoveEvent *>(event);
            if (de->mimeData()->hasFormat(QLatin1String(kMime))) {
                de->acceptProposedAction();
                return true;
            }
        } else if (event->type() == QEvent::Drop) {
            auto *de = static_cast<QDropEvent *>(event);
            if (de->mimeData()->hasFormat(QLatin1String(kMime))) {
                QListWidgetItem *target = m_groups->itemAt(de->position().toPoint());
                if (target) {
                    const QStringList ids = QString::fromUtf8(de->mimeData()->data(QLatin1String(kMime))).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
                    DeviceRegistry::instance().assignGroup(ids, target->data(Qt::UserRole).toString());
                }
                de->acceptProposedAction();
                return true;
            }
        }
    }
    return QWidget::eventFilter(watched, event);
}

} // namespace farm
