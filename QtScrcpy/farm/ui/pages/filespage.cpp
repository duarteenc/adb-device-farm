#include "filespage.h"

#include <QFileInfo>
#include <QLocale>
#include <QComboBox>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QMimeData>
#include <QPushButton>
#include <QStandardPaths>
#include <QTableWidget>
#include <QUrl>
#include <QVBoxLayout>

#include "core/farmsettings.h"
#include "devices/devicecommands.h"
#include "devices/deviceregistry.h"
#include "ui/farmtheme.h"
#include "ui/widgets/batchjobdialog.h"

namespace farm {

FilesPage::FilesPage(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("page"));
    setAcceptDrops(true);
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(20, 16, 20, 16);
    root->setSpacing(10);
    auto *title = new QLabel(tr("Files"), this);
    title->setObjectName(QStringLiteral("pageTitle"));
    root->addWidget(title);

    auto *top = new QHBoxLayout();
    top->addWidget(new QLabel(tr("Browse device:"), this));
    m_reference = new QComboBox(this);
    m_reference->setMinimumWidth(240);
    top->addWidget(m_reference);
    m_pathEdit = new QLineEdit(m_path, this);
    top->addWidget(m_pathEdit, 1);
    auto *goBtn = theme::button(tr("Go"), this);
    auto *upBtn = theme::button(tr("Up"), this);
    top->addWidget(goBtn);
    top->addWidget(upBtn);
    root->addLayout(top);

    auto *shortcuts = new QHBoxLayout();
    for (const QString &s : { QStringLiteral("/sdcard"), QStringLiteral("/sdcard/Download"), QStringLiteral("/sdcard/DCIM"), QStringLiteral("/sdcard/Pictures"), QStringLiteral("/sdcard/Documents"), QStringLiteral("/sdcard/Movies") }) {
        auto *b = theme::button(s.section(QLatin1Char('/'), -1), this, QStringLiteral("quiet"));
        b->setToolTip(s);
        connect(b, &QPushButton::clicked, this, [this, s]() { navigate(s); });
        shortcuts->addWidget(b);
    }
    shortcuts->addStretch(1);
    m_scope = new QComboBox(this);
    m_scope->addItem(tr("Uploads → selected devices"), QStringLiteral("selection"));
    m_scope->addItem(tr("Uploads → this device only"), QStringLiteral("reference"));
    m_scope->addItem(tr("Uploads → all online devices"), QStringLiteral("all"));
    shortcuts->addWidget(m_scope);
    m_targetLabel = new QLabel(this);
    m_targetLabel->setObjectName(QStringLiteral("hint"));
    shortcuts->addWidget(m_targetLabel);
    root->addLayout(shortcuts);

    m_table = new QTableWidget(0, 4, this);
    m_table->setHorizontalHeaderLabels({ tr("Name"), tr("Size"), tr("Modified"), tr("Permissions") });
    m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_table->verticalHeader()->setVisible(false);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    root->addWidget(m_table, 1);

    auto *actions = new QHBoxLayout();
    auto *uploadBtn = theme::button(tr("Upload files to targets…"), this, QStringLiteral("primary"));
    auto *mkdirBtn = theme::button(tr("New folder on targets…"), this);
    auto *pullBtn = theme::button(tr("Download selected"), this);
    auto *deleteBtn = theme::button(tr("Delete selected on targets"), this, QStringLiteral("danger"));
    actions->addWidget(uploadBtn);
    actions->addWidget(mkdirBtn);
    actions->addWidget(pullBtn);
    actions->addWidget(deleteBtn);
    actions->addStretch(1);
    root->addLayout(actions);
    m_status = theme::hint(tr("Drop files anywhere on this page to upload them to the current folder on the target devices."), this);
    root->addWidget(m_status);

    connect(goBtn, &QPushButton::clicked, this, [this]() { navigate(m_pathEdit->text().trimmed()); });
    connect(m_pathEdit, &QLineEdit::returnPressed, this, [this]() { navigate(m_pathEdit->text().trimmed()); });
    connect(upBtn, &QPushButton::clicked, this, [this]() {
        QString p = m_path;
        while (p.endsWith(QLatin1Char('/')) && p.size() > 1) {
            p.chop(1);
        }
        const int slash = p.lastIndexOf(QLatin1Char('/'));
        navigate(slash <= 0 ? QStringLiteral("/") : p.left(slash));
    });
    connect(m_reference, &QComboBox::currentIndexChanged, this, [this](int) { reload(); });
    connect(m_scope, &QComboBox::currentIndexChanged, this, [this](int) { setTargets(m_targets); });
    connect(m_table, &QTableWidget::itemDoubleClicked, this, [this](QTableWidgetItem *it) {
        const int row = it->row();
        if (row >= 0 && row < m_entries.size() && m_entries.at(row).isDir) {
            navigate(m_path + (m_path.endsWith(QLatin1Char('/')) ? QString() : QStringLiteral("/")) + m_entries.at(row).name);
        }
    });
    connect(uploadBtn, &QPushButton::clicked, this, [this]() {
        const QStringList paths = QFileDialog::getOpenFileNames(this, tr("Choose files to upload"), FarmSettings::instance().stringValue(QStringLiteral("ui/lastUploadDir"), QString()));
        if (paths.isEmpty()) {
            return;
        }
        FarmSettings::instance().setValue(QStringLiteral("ui/lastUploadDir"), QFileInfo(paths.first()).absolutePath());
        const QStringList ids = targetIds();
        if (ids.isEmpty()) {
            m_status->setText(tr("No target devices."));
            return;
        }
        BatchJobDialog::show(DeviceCommands::pushFiles(ids, paths, m_path), this);
    });
    connect(mkdirBtn, &QPushButton::clicked, this, [this]() {
        bool ok = false;
        const QString name = QInputDialog::getText(this, tr("New folder"), tr("Folder name (inside %1):").arg(m_path), QLineEdit::Normal, QString(), &ok).trimmed();
        if (ok && !name.isEmpty()) {
            BatchJobDialog::show(DeviceCommands::makeDirectory(targetIds(), m_path + QLatin1Char('/') + name), this);
        }
    });
    connect(pullBtn, &QPushButton::clicked, this, [this]() {
        const QStringList remote = selectedRemotePaths();
        if (remote.isEmpty()) {
            return;
        }
        const QString dir = QFileDialog::getExistingDirectory(this, tr("Download into"), QStandardPaths::writableLocation(QStandardPaths::DownloadLocation));
        if (dir.isEmpty()) {
            return;
        }
        for (const QString &r : remote) {
            BatchJobDialog::show(DeviceCommands::pullFile(targetIds(), r, dir), this);
        }
    });
    connect(deleteBtn, &QPushButton::clicked, this, [this]() {
        const QStringList remote = selectedRemotePaths();
        const QStringList ids = targetIds();
        if (remote.isEmpty() || ids.isEmpty()) {
            return;
        }
        if (QMessageBox::warning(this, tr("Delete"), tr("Delete %1 item(s) on %2 device(s)?\n\n%3").arg(remote.size()).arg(ids.size()).arg(remote.join(QLatin1Char('\n'))),
                                 QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes) {
            return;
        }
        for (const QString &r : remote) {
            BatchJob *job = DeviceCommands::deleteRemote(ids, r);
            connect(job, &BatchJob::finished, this, [this](BatchJob::Status) { reload(); });
            BatchJobDialog::show(job, this);
        }
    });
    connect(&DeviceRegistry::instance(), &DeviceRegistry::stateChanged, this, [this](const QString &, DeviceState, DeviceState) { setTargets(m_targets); });
    setTargets(QStringList());
}

void FilesPage::setTargets(const QStringList &ids)
{
    m_targets = ids;
    const QString prev = m_reference->currentData().toString();
    m_reference->blockSignals(true);
    m_reference->clear();
    const QStringList online = DeviceRegistry::instance().sorted(DeviceRegistry::SortKey::Number, true, DeviceRegistry::instance().onlineIds());
    for (const QString &id : online) {
        const DeviceRecord r = DeviceRegistry::instance().get(id);
        m_reference->addItem(QStringLiteral("%1 %2 (%3)").arg(r.numberString(), r.displayName(), id), id);
    }
    int idx = m_reference->findData(prev);
    if (idx < 0 && !ids.isEmpty()) {
        idx = m_reference->findData(ids.first());
    }
    m_reference->setCurrentIndex(std::max(0, idx));
    m_reference->blockSignals(false);
    m_targetLabel->setText(tr("%n target(s)", nullptr, static_cast<int>(targetIds().size())));
    if (prev != m_reference->currentData().toString() || m_entries.isEmpty()) {
        reload();
    }
}

QStringList FilesPage::targetIds() const
{
    const QString scope = m_scope->currentData().toString();
    if (scope == QLatin1String("all")) {
        return DeviceRegistry::instance().onlineIds();
    }
    if (scope == QLatin1String("reference")) {
        return referenceId().isEmpty() ? QStringList() : QStringList{ referenceId() };
    }
    return m_targets.isEmpty() && !referenceId().isEmpty() ? QStringList{ referenceId() } : m_targets;
}

QString FilesPage::referenceId() const
{
    return m_reference->currentData().toString();
}

QStringList FilesPage::selectedRemotePaths() const
{
    QStringList list;
    const QList<QTableWidgetSelectionRange> ranges = m_table->selectedRanges();
    for (const QTableWidgetSelectionRange &r : ranges) {
        for (int row = r.topRow(); row <= r.bottomRow(); ++row) {
            if (row < m_entries.size()) {
                list << m_path + (m_path.endsWith(QLatin1Char('/')) ? QString() : QStringLiteral("/")) + m_entries.at(row).name;
            }
        }
    }
    return list;
}

void FilesPage::navigate(const QString &path)
{
    m_path = path.isEmpty() ? QStringLiteral("/sdcard") : path;
    m_pathEdit->setText(m_path);
    reload();
}

void FilesPage::reload()
{
    const QString id = referenceId();
    if (id.isEmpty()) {
        m_table->setRowCount(0);
        m_status->setText(tr("No online device to browse."));
        return;
    }
    m_status->setText(tr("Listing %1 on %2…").arg(m_path, id));
    DeviceCommands::listDirectory(id, m_path, this, [this, id](QList<adb::RemoteEntry> entries, QString error) {
        if (!error.isEmpty()) {
            m_status->setText(tr("ls failed: %1").arg(error));
            return;
        }
        m_entries = entries;
        m_table->setRowCount(static_cast<int>(entries.size()));
        for (int i = 0; i < entries.size(); ++i) {
            const adb::RemoteEntry &e = entries.at(i);
            auto *name = new QTableWidgetItem((e.isDir ? QStringLiteral("▸ ") : QStringLiteral("   ")) + e.name);
            if (e.isDir) {
                name->setForeground(theme::accent());
            }
            m_table->setItem(i, 0, name);
            m_table->setItem(i, 1, new QTableWidgetItem(e.isDir ? QString() : QLocale().formattedDataSize(e.size)));
            m_table->setItem(i, 2, new QTableWidgetItem(e.modified));
            m_table->setItem(i, 3, new QTableWidgetItem(e.permissions));
        }
        m_status->setText(tr("%n item(s) in %1 on %2", nullptr, static_cast<int>(entries.size())).arg(m_path, id));
    });
}

void FilesPage::dragEnterEvent(QDragEnterEvent *event)
{
    if (event->mimeData()->hasUrls()) {
        event->acceptProposedAction();
    }
}

void FilesPage::dropEvent(QDropEvent *event)
{
    QStringList paths;
    for (const QUrl &u : event->mimeData()->urls()) {
        if (u.isLocalFile()) {
            paths << u.toLocalFile();
        }
    }
    const QStringList ids = targetIds();
    if (!paths.isEmpty() && !ids.isEmpty()) {
        BatchJob *job = DeviceCommands::pushFiles(ids, paths, m_path);
        connect(job, &BatchJob::finished, this, [this](BatchJob::Status) { reload(); });
        BatchJobDialog::show(job, this);
    }
    event->acceptProposedAction();
}

} // namespace farm
