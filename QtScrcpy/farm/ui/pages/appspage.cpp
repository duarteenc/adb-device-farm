#include "appspage.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QMimeData>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTableWidget>
#include <QUrl>
#include <QVBoxLayout>
#include <QTimer>

#include "core/farmsettings.h"
#include "devices/devicecommands.h"
#include "devices/deviceregistry.h"
#include "ui/farmtheme.h"
#include "ui/widgets/batchjobdialog.h"

namespace farm {

AppsPage::AppsPage(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("page"));
    setAcceptDrops(true);
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(20, 16, 20, 16);
    root->setSpacing(10);
    auto *title = new QLabel(tr("Applications"), this);
    title->setObjectName(QStringLiteral("pageTitle"));
    root->addWidget(title);

    auto *top = new QHBoxLayout();
    top->addWidget(new QLabel(tr("Package list from:"), this));
    m_reference = new QComboBox(this);
    m_reference->setMinimumWidth(240);
    top->addWidget(m_reference);
    m_thirdParty = new QCheckBox(tr("Third-party only"), this);
    m_thirdParty->setChecked(true);
    top->addWidget(m_thirdParty);
    m_filter = new QLineEdit(this);
    m_filter->setPlaceholderText(tr("Filter packages…"));
    m_filter->setClearButtonEnabled(true);
    top->addWidget(m_filter, 1);
    auto *reload = theme::button(tr("Reload"), this);
    top->addWidget(reload);
    root->addLayout(top);

    auto *scopeRow = new QHBoxLayout();
    scopeRow->addWidget(new QLabel(tr("Batch actions apply to:"), this));
    m_scope = new QComboBox(this);
    m_scope->addItem(tr("Selected devices (from Devices page)"), QStringLiteral("selection"));
    m_scope->addItem(tr("Reference device only"), QStringLiteral("reference"));
    m_scope->addItem(tr("All online devices"), QStringLiteral("all"));
    scopeRow->addWidget(m_scope);
    m_targetLabel = new QLabel(this);
    m_targetLabel->setObjectName(QStringLiteral("hint"));
    scopeRow->addWidget(m_targetLabel, 1);
    root->addLayout(scopeRow);

    auto *body = new QHBoxLayout();
    m_table = new QTableWidget(0, 2, this);
    m_table->setHorizontalHeaderLabels({ tr("Package"), tr("APK path") });
    m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setStretchLastSection(true);
    m_table->verticalHeader()->setVisible(false);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    body->addWidget(m_table, 3);

    auto *side = new QVBoxLayout();
    side->addWidget(theme::sectionTitle(tr("Install"), this));
    auto *installBtn = theme::button(tr("Install APK on targets…"), this, QStringLiteral("primary"));
    side->addWidget(installBtn);
    side->addWidget(theme::hint(tr("Or drop .apk files anywhere on this page."), this));
    side->addWidget(theme::sectionTitle(tr("Selected package → targets"), this));
    auto act = [&](const QString &label, const QString &role, std::function<void(const QString &, const QStringList &)> fn) {
        auto *b = theme::button(label, this, role);
        connect(b, &QPushButton::clicked, this, [this, fn]() {
            const QString pkg = selectedPackage();
            const QStringList ids = targetIds();
            if (pkg.isEmpty() || ids.isEmpty()) {
                m_status->setText(tr("Select a package and make sure there are target devices."));
                return;
            }
            fn(pkg, ids);
        });
        side->addWidget(b);
    };
    act(tr("Launch"), QString(), [this](const QString &p, const QStringList &ids) { BatchJobDialog::show(DeviceCommands::launchApp(ids, p), this); });
    act(tr("Force stop"), QString(), [this](const QString &p, const QStringList &ids) { BatchJobDialog::show(DeviceCommands::forceStop(ids, p), this); });
    act(tr("Open App Info"), QString(), [this](const QString &p, const QStringList &ids) { BatchJobDialog::show(DeviceCommands::openAppInfo(ids, p), this); });
    act(tr("Clear cache"), QString(), [this](const QString &p, const QStringList &ids) { BatchJobDialog::show(DeviceCommands::clearCache(ids, p), this); });
    act(tr("Grant permission…"), QString(), [this](const QString &p, const QStringList &ids) {
        bool ok = false;
        const QString perm = QInputDialog::getText(this, tr("Grant permission"), tr("Permission (e.g. android.permission.CAMERA):"), QLineEdit::Normal, QStringLiteral("android.permission."), &ok).trimmed();
        if (ok && !perm.isEmpty()) {
            BatchJobDialog::show(DeviceCommands::setPermission(ids, p, perm, true), this);
        }
    });
    act(tr("Revoke permission…"), QString(), [this](const QString &p, const QStringList &ids) {
        bool ok = false;
        const QString perm = QInputDialog::getText(this, tr("Revoke permission"), tr("Permission:"), QLineEdit::Normal, QStringLiteral("android.permission."), &ok).trimmed();
        if (ok && !perm.isEmpty()) {
            BatchJobDialog::show(DeviceCommands::setPermission(ids, p, perm, false), this);
        }
    });
    act(tr("Clear app data"), QStringLiteral("danger"), [this](const QString &p, const QStringList &ids) {
        if (QMessageBox::warning(this, tr("Clear data"), tr("Clear ALL data of %1?\n\nThis operation will affect %2 device(s).").arg(p).arg(ids.size()), QMessageBox::Yes | QMessageBox::No, QMessageBox::No) == QMessageBox::Yes) {
            BatchJobDialog::show(DeviceCommands::clearData(ids, p), this);
        }
    });
    act(tr("Uninstall"), QStringLiteral("danger"), [this](const QString &p, const QStringList &ids) {
        if (QMessageBox::warning(this, tr("Uninstall"), tr("Uninstall %1?\n\nThis operation will affect %2 device(s).").arg(p).arg(ids.size()), QMessageBox::Yes | QMessageBox::No, QMessageBox::No) == QMessageBox::Yes) {
            BatchJobDialog::show(DeviceCommands::uninstall(ids, p), this);
        }
    });
    side->addWidget(theme::sectionTitle(tr("Details (reference device)"), this));
    m_details = new QPlainTextEdit(this);
    m_details->setReadOnly(true);
    side->addWidget(m_details, 1);
    auto *sideW = new QWidget(this);
    sideW->setLayout(side);
    sideW->setFixedWidth(300);
    body->addWidget(sideW);
    root->addLayout(body, 1);
    m_status = theme::hint(QString(), this);
    root->addWidget(m_status);

    connect(reload, &QPushButton::clicked, this, &AppsPage::reloadPackages);
    connect(m_reference, &QComboBox::currentIndexChanged, this, [this](int) { reloadPackages(); });
    connect(m_thirdParty, &QCheckBox::toggled, this, [this](bool) { reloadPackages(); });
    connect(m_filter, &QLineEdit::textChanged, this, [this](const QString &) { filterRows(); });
    connect(m_scope, &QComboBox::currentIndexChanged, this, [this](int) { setTargets(m_targets); });
    connect(m_table, &QTableWidget::itemSelectionChanged, this, [this]() { showDetails(selectedPackage()); });
    connect(installBtn, &QPushButton::clicked, this, [this]() {
        const QString path = QFileDialog::getOpenFileName(this, tr("Choose APK"), FarmSettings::instance().stringValue(QStringLiteral("ui/lastApkDir"), QString()), tr("Android packages (*.apk)"));
        if (path.isEmpty()) {
            return;
        }
        FarmSettings::instance().setValue(QStringLiteral("ui/lastApkDir"), QFileInfo(path).absolutePath());
        const QStringList ids = targetIds();
        if (ids.isEmpty()) {
            m_status->setText(tr("No target devices."));
            return;
        }
        BatchJobDialog::show(DeviceCommands::installApk(ids, path), this);
    });
    auto *targetsTimer = new QTimer(this);    // coalesce state bursts: one refresh per 150 ms
    targetsTimer->setSingleShot(true);
    targetsTimer->setInterval(150);
    connect(targetsTimer, &QTimer::timeout, this, [this]() { setTargets(m_targets); });
    connect(&DeviceRegistry::instance(), &DeviceRegistry::stateChanged, targetsTimer, [targetsTimer](const QString &, DeviceState, DeviceState) { targetsTimer->start(); });
    setTargets(QStringList());
}

void AppsPage::setTargets(const QStringList &ids)
{
    m_targets = ids;
    const QString prevRef = m_reference->currentData().toString();
    m_reference->blockSignals(true);
    m_reference->clear();
    const QStringList online = DeviceRegistry::instance().sorted(DeviceRegistry::SortKey::Number, true, DeviceRegistry::instance().onlineIds());
    for (const QString &id : online) {
        const DeviceRecord r = DeviceRegistry::instance().get(id);
        m_reference->addItem(QStringLiteral("%1 %2 (%3)").arg(r.numberString(), r.displayName(), id), id);
    }
    int idx = m_reference->findData(prevRef);
    if (idx < 0 && !ids.isEmpty()) {
        idx = m_reference->findData(ids.first());
    }
    m_reference->setCurrentIndex(std::max(0, idx));
    m_reference->blockSignals(false);
    m_targetLabel->setText(tr("%n target device(s)", nullptr, static_cast<int>(targetIds().size())));
    if (m_packages.isEmpty() || prevRef != m_reference->currentData().toString()) {
        reloadPackages();
    }
}

QStringList AppsPage::targetIds() const
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

QString AppsPage::referenceId() const
{
    return m_reference->currentData().toString();
}

QString AppsPage::selectedPackage() const
{
    const int row = m_table->currentRow();
    return row >= 0 && m_table->item(row, 0) ? m_table->item(row, 0)->text() : QString();
}

void AppsPage::reloadPackages()
{
    const QString id = referenceId();
    if (id.isEmpty()) {
        m_table->setRowCount(0);
        m_status->setText(tr("No online device to read packages from."));
        return;
    }
    const bool third = m_thirdParty->isChecked();
    m_status->setText(tr("Loading packages from %1…").arg(id));
    DeviceCommands::listPackages(id, third, this, [this, id, third](QList<adb::PackageInfo> list, QString error) {
        if (id != referenceId() || third != m_thirdParty->isChecked()) {
            return;    // stale reply: the reference device or the filter changed while pm ran
        }
        if (!error.isEmpty()) {
            m_status->setText(tr("pm list packages failed on %1: %2").arg(id, error));
            return;
        }
        m_packages = list;
        m_table->setRowCount(static_cast<int>(list.size()));
        for (int i = 0; i < list.size(); ++i) {
            m_table->setItem(i, 0, new QTableWidgetItem(list.at(i).name));
            m_table->setItem(i, 1, new QTableWidgetItem(list.at(i).apkPath));
        }
        filterRows();
        m_status->setText(tr("%n package(s) on %1", nullptr, static_cast<int>(list.size())).arg(id));
    });
}

void AppsPage::filterRows()
{
    const QString f = m_filter->text().trimmed();
    for (int i = 0; i < m_table->rowCount(); ++i) {
        m_table->setRowHidden(i, !f.isEmpty() && !m_table->item(i, 0)->text().contains(f, Qt::CaseInsensitive));
    }
}

void AppsPage::showDetails(const QString &package)
{
    if (package.isEmpty() || referenceId().isEmpty()) {
        m_details->clear();
        return;
    }
    m_details->setPlainText(tr("Loading %1…").arg(package));
    DeviceCommands::packageDetails(referenceId(), package, this, [this, package](DeviceCommands::PackageDetails d, QString error) {
        if (selectedPackage() != package) {
            return;
        }
        if (!error.isEmpty()) {
            m_details->setPlainText(error);
            return;
        }
        m_details->setPlainText(tr("Package: %1\nVersion: %2 (%3)\nTarget SDK: %4\nAPK: %5\n\nGranted permissions:\n  %6\n\nOther requested permissions:\n  %7")
                                    .arg(d.package, d.versionName, d.versionCode, d.targetSdk, d.apkPath, d.grantedPermissions.join(QStringLiteral("\n  ")), d.requestedPermissions.join(QStringLiteral("\n  "))));
    });
}

void AppsPage::dragEnterEvent(QDragEnterEvent *event)
{
    if (event->mimeData()->hasUrls()) {
        event->acceptProposedAction();
    }
}

void AppsPage::dropEvent(QDropEvent *event)
{
    const QStringList ids = targetIds();
    for (const QUrl &u : event->mimeData()->urls()) {
        if (u.isLocalFile() && u.toLocalFile().endsWith(QLatin1String(".apk"), Qt::CaseInsensitive) && !ids.isEmpty()) {
            BatchJobDialog::show(DeviceCommands::installApk(ids, u.toLocalFile()), this);
        }
    }
    event->acceptProposedAction();
}

} // namespace farm
