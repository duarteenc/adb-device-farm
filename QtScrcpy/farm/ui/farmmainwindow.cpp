#include "farmmainwindow.h"

#include <QKeySequence>
#include <QApplication>
#include <QCloseEvent>
#include <QHBoxLayout>
#include <QIcon>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QProgressBar>
#include <QShortcut>
#include <QStackedWidget>
#include <QStatusBar>
#include <QSystemTrayIcon>
#include <QVBoxLayout>

#include "core/activitylog.h"
#include "core/appcontext.h"
#include "core/batchjob.h"
#include "core/farmsettings.h"
#include "devices/devicehealthmonitor.h"
#include "devices/deviceregistry.h"
#include "devices/deviceservice.h"
#include "discovery/devicediscoveryservice.h"
#include "farmtheme.h"
#include "pages/activitypage.h"
#include "pages/adbconsolepage.h"
#include "pages/appspage.h"
#include "pages/automationspage.h"
#include "pages/dashboardpage.h"
#include "pages/devicespage.h"
#include "pages/filespage.h"
#include "pages/groupspage.h"
#include "pages/performancepage.h"
#include "pages/schedulerpage.h"
#include "pages/settingspage.h"
#include "performance/perfmonitor.h"
#include "widgets/deviceinspector.h"

namespace farm {

namespace {
struct NavEntry
{
    const char *key;
    const char *label;
    const char *glyph;
};
const NavEntry kNav[] = {
    { "dashboard", "Dashboard", "▦" }, { "devices", "Devices", "▣" },   { "groups", "Groups", "☷" },
    { "automations", "Automations", "⚙" }, { "scheduler", "Scheduler", "⏰" }, { "apps", "Applications", "◎" },
    { "files", "Files", "☰" },        { "console", "ADB Console", "›" }, { "activity", "Activity", "≡" },
    { "performance", "Performance", "≈" }, { "settings", "Settings", "⚒" },
};
} // namespace

FarmMainWindow::FarmMainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    setWindowTitle(tr("ADB Device Farm"));
    setObjectName(QStringLiteral("farmRoot"));
    theme::apply(this);
    setMinimumSize(1100, 680);

    auto *central = new QWidget(this);
    central->setObjectName(QStringLiteral("farmRoot"));
    auto *root = new QHBoxLayout(central);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);
    root->addWidget(buildNav(), 0);

    auto *right = new QVBoxLayout();
    right->setContentsMargins(0, 0, 0, 0);
    right->setSpacing(0);
    right->addWidget(buildTopBar(), 0);
    m_stack = new QStackedWidget(central);
    right->addWidget(m_stack, 1);
    root->addLayout(right, 1);
    setCentralWidget(central);

    // ---- pages ----
    m_dashboard = new DashboardPage(this);
    m_devices = new DevicesPage(this);
    m_groups = new GroupsPage(this);
    m_automations = new AutomationsPage(this);
    m_scheduler = new SchedulerPage(this);
    m_apps = new AppsPage(this);
    m_files = new FilesPage(this);
    m_console = new AdbConsolePage(this);
    m_activity = new ActivityPage(this);
    m_performance = new PerformancePage(this);
    m_settings = new SettingsPage(this);
    QWidget *pages[] = { m_dashboard, m_devices, m_groups, m_automations, m_scheduler, m_apps, m_files, m_console, m_activity, m_performance, m_settings };
    int idx = 0;
    for (const NavEntry &e : kNav) {
        m_pageIndex.insert(QLatin1String(e.key), idx);
        m_stack->addWidget(pages[idx]);
        ++idx;
    }

    // ---- cross-page wiring ----
    connect(m_devices, &DevicesPage::consoleRequested, this, [this](const QStringList &ids) {
        m_console->setTargets(ids);
        showPage(QStringLiteral("console"));
    });
    connect(m_devices, &DevicesPage::appsRequested, this, [this](const QStringList &ids) {
        m_apps->setTargets(ids);
        showPage(QStringLiteral("apps"));
    });
    connect(m_devices, &DevicesPage::filesRequested, this, [this](const QStringList &ids) {
        m_files->setTargets(ids);
        showPage(QStringLiteral("files"));
    });
    connect(m_devices, &DevicesPage::automationRequested, this, [this](const QStringList &ids) {
        m_automations->setTargets(ids);
        showPage(QStringLiteral("automations"));
    });
    connect(m_devices, &DevicesPage::inspectRequested, this, [this](const QString &id) {
        auto *dlg = new DeviceInspector(id, this);
        dlg->setAttribute(Qt::WA_DeleteOnClose);
        dlg->show();
    });
    connect(m_devices, &DevicesPage::statusMessage, this, [this](const QString &t) { m_statusText->setText(t); });
    connect(m_dashboard, &DashboardPage::navigate, this, &FarmMainWindow::showPage);
    connect(m_dashboard, &DashboardPage::deviceActivated, this, [this](const QString &id) {
        showPage(QStringLiteral("devices"));
        m_devices->focusDevice(id);
    });
    connect(m_groups, &GroupsPage::showGroupRequested, this, [this](const QString &group) {
        m_devices->setGroupFilter(group);
        showPage(QStringLiteral("devices"));
    });
    connect(m_groups, &GroupsPage::selectGroupRequested, this, [this](const QString &group) {
        m_devices->selectIds(DeviceRegistry::instance().membersOf(group));
        showPage(QStringLiteral("devices"));
    });
    connect(m_activity, &ActivityPage::deviceActivated, this, [this](const QString &id) {
        showPage(QStringLiteral("devices"));
        m_devices->focusDevice(id);
    });
    connect(m_automations, &AutomationsPage::selectionNeeded, this, [this]() {
        m_automations->setTargets(m_devices->selectedIds());
    });

    // ---- discovery / status ----
    DeviceDiscoveryService &discovery = DeviceDiscoveryService::instance();
    connect(&discovery, &DeviceDiscoveryService::scanStarted, this, [this](int total) {
        m_scanBar->setRange(0, total);
        m_scanBar->setValue(0);
        m_scanBar->show();
        m_scanLabel->setText(tr("Scanning %1…").arg(FarmSettings::instance().subnet()));
    });
    connect(&discovery, &DeviceDiscoveryService::scanProgress, this, [this](int done, int) { m_scanBar->setValue(done); });
    connect(&discovery, &DeviceDiscoveryService::scanFinished, this, [this](int found, int connected, qint64 ms) {
        m_scanBar->hide();
        m_scanLabel->setText(tr("Last scan: %1 hosts, %2 new, %3 ms").arg(found).arg(connected).arg(ms));
    });
    connect(&discovery, &DeviceDiscoveryService::snapshotUpdated, this, [this](const QList<adb::AdbDeviceInfo> &) {
        if (!m_firstSignal) {
            m_firstSignal = true;
            emit firstDevicesReady();
        }
        refreshCounters();
    });
    connect(&discovery, &DeviceDiscoveryService::statusMessage, this, [this](const QString &t) { m_statusText->setText(t); });
    connect(&DeviceRegistry::instance(), &DeviceRegistry::stateChanged, this, [this](const QString &id, DeviceState oldState, DeviceState newState) {
        refreshCounters();
        if (deviceStateIsOnline(oldState) && !deviceStateIsOnline(newState) && FarmSettings::instance().notifyCategory(QStringLiteral("deviceDisconnected"))) {
            notify(tr("Device disconnected"), DeviceRegistry::instance().get(id).displayName() + QStringLiteral(" (") + id + QLatin1Char(')'), true);
        }
    });
    connect(&DeviceHealthMonitor::instance(), &DeviceHealthMonitor::batteryBelow, this, [this](const QString &id, int level) {
        if (FarmSettings::instance().notifyCategory(QStringLiteral("batteryLow"))) {
            notify(tr("Battery low"), tr("%1: %2%").arg(DeviceRegistry::instance().get(id).displayName()).arg(level), true);
        }
    });
    connect(&DeviceHealthMonitor::instance(), &DeviceHealthMonitor::temperatureAbove, this, [this](const QString &id, double c) {
        if (FarmSettings::instance().notifyCategory(QStringLiteral("temperatureHigh"))) {
            notify(tr("Temperature high"), tr("%1: %2 °C").arg(DeviceRegistry::instance().get(id).displayName()).arg(c, 0, 'f', 1), true);
        }
    });
    connect(&JobManager::instance(), &JobManager::jobAdded, this, [this](BatchJob *job) {
        connect(job, &BatchJob::finished, this, [this, job](BatchJob::Status status) {
            const bool failed = status == BatchJob::Failed || job->failed() > 0;
            const QString cat = job->kind() == QLatin1String("install") ? QStringLiteral("apkFailed") : (failed ? QStringLiteral("automationFailed") : QStringLiteral("automationCompleted"));
            if (job->kind() == QLatin1String("install") && !failed) {
                return;
            }
            if (FarmSettings::instance().notifyCategory(cat)) {
                notify(job->name(), job->summary(), failed);
            }
        });
    });
    connect(&PerfMonitor::instance(), &PerfMonitor::sampled, this, [this](const PerfSnapshot &s) {
        m_perfText->setText(tr("CPU %1%  ·  RAM %2 MB  ·  %3 fps rendered  ·  ADB %4/s  ·  UI lag %5 ms")
                                .arg(s.cpuPercent, 0, 'f', 1)
                                .arg(s.workingSetMb)
                                .arg(s.renderedFps, 0, 'f', 0)
                                .arg(s.adbOpsPerSec, 0, 'f', 1)
                                .arg(s.uiLagMaxMs));
    });

    statusBar()->setSizeGripEnabled(true);
    m_statusText = new QLabel(tr("Ready."), this);
    m_perfText = new QLabel(this);
    m_perfText->setObjectName(QStringLiteral("hint"));
    statusBar()->addWidget(m_statusText, 1);
    statusBar()->addPermanentWidget(m_perfText);

    setupTray();
    m_counterTimer.setInterval(2000);
    connect(&m_counterTimer, &QTimer::timeout, this, &FarmMainWindow::refreshCounters);
    m_counterTimer.start();

    // Shortcuts
    new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_F), this, [this]() {
        showPage(QStringLiteral("devices"));
        m_search->setFocus();
        m_search->selectAll();
    });
    new QShortcut(QKeySequence(Qt::Key_F5), this, []() { DeviceDiscoveryService::instance().quickRefresh(); });
    new QShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_F5), this, []() { DeviceDiscoveryService::instance().fullScan(); });
    new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_Comma), this, [this]() { showPage(QStringLiteral("settings")); });
    for (int i = 0; i < 9 && i < static_cast<int>(sizeof(kNav) / sizeof(kNav[0])); ++i) {
        const QString key = QLatin1String(kNav[i].key);
        new QShortcut(QKeySequence(Qt::CTRL | (Qt::Key_1 + i)), this, [this, key]() { showPage(key); });
    }

    showPage(FarmSettings::instance().lastPage());
    refreshCounters();
}

FarmMainWindow::~FarmMainWindow() = default;

QWidget *FarmMainWindow::buildNav()
{
    auto *nav = new QWidget(this);
    nav->setObjectName(QStringLiteral("navBar"));
    nav->setFixedWidth(176);
    auto *lay = new QVBoxLayout(nav);
    lay->setContentsMargins(0, 10, 0, 10);
    lay->setSpacing(4);
    auto *title = new QLabel(tr("ADB Device Farm"), nav);
    title->setStyleSheet(QStringLiteral("font-size:15px; font-weight:bold; padding:6px 14px 10px 14px;"));
    lay->addWidget(title);
    m_nav = new QListWidget(nav);
    for (const NavEntry &e : kNav) {
        auto *item = new QListWidgetItem(QStringLiteral("%1   %2").arg(QString::fromUtf8(e.glyph), tr(e.label)), m_nav);
        item->setData(Qt::UserRole, QLatin1String(e.key));
    }
    connect(m_nav, &QListWidget::currentRowChanged, this, [this](int row) {
        if (row >= 0) {
            m_stack->setCurrentIndex(row);
            FarmSettings::instance().setValue(QStringLiteral("ui/lastPage"), m_nav->item(row)->data(Qt::UserRole).toString());
        }
    });
    lay->addWidget(m_nav, 1);
    auto *version = new QLabel(QStringLiteral("v%1").arg(AppContext::instance().version()), nav);
    version->setObjectName(QStringLiteral("hint"));
    version->setAlignment(Qt::AlignCenter);
    lay->addWidget(version);
    return nav;
}

QWidget *FarmMainWindow::buildTopBar()
{
    auto *bar = new QWidget(this);
    bar->setObjectName(QStringLiteral("topBar"));
    bar->setFixedHeight(44);
    auto *lay = new QHBoxLayout(bar);
    lay->setContentsMargins(12, 6, 12, 6);
    lay->setSpacing(10);
    m_search = new QLineEdit(bar);
    m_search->setPlaceholderText(tr("Global search (Ctrl+F): number, name, serial, IP, model, group"));
    m_search->setClearButtonEnabled(true);
    m_search->setFixedWidth(360);
    connect(m_search, &QLineEdit::textChanged, this, [this](const QString &t) {
        m_devices->setSearchQuery(t);
        if (!t.isEmpty() && m_stack->currentWidget() != m_devices) {
            showPage(QStringLiteral("devices"));
        }
    });
    m_counters = new QLabel(bar);
    m_scanLabel = new QLabel(bar);
    m_scanLabel->setObjectName(QStringLiteral("hint"));
    m_scanBar = new QProgressBar(bar);
    m_scanBar->setFixedWidth(140);
    m_scanBar->setTextVisible(false);
    m_scanBar->hide();
    lay->addWidget(m_search);
    lay->addWidget(m_counters);
    lay->addStretch(1);
    lay->addWidget(m_scanLabel);
    lay->addWidget(m_scanBar);
    return bar;
}

void FarmMainWindow::refreshCounters()
{
    const DeviceRegistry &registry = DeviceRegistry::instance();
    const int known = registry.count();
    const int online = static_cast<int>(registry.onlineIds().size());
    const int mirroring = AppContext::instance().isMock() ? registry.countInState(DeviceState::Mirroring) : DeviceService::instance().mirroringCount();
    const int problems = registry.countInState(DeviceState::Unauthorized) + registry.countInState(DeviceState::Error);
    m_counters->setText(tr("<b>%1</b> online · <b>%2</b> mirroring · %3 known%4")
                            .arg(online)
                            .arg(mirroring)
                            .arg(known)
                            .arg(problems > 0 ? tr(" · <span style='color:%1'>%2 need attention</span>").arg(theme::warning().name()).arg(problems) : QString()));
    if (m_tray) {
        m_tray->setToolTip(tr("ADB Device Farm — %1 online, %2 mirroring").arg(online).arg(mirroring));
    }
}

void FarmMainWindow::showPage(const QString &key)
{
    const int idx = m_pageIndex.value(key, 1);
    m_nav->setCurrentRow(idx);
    m_stack->setCurrentIndex(idx);
}

void FarmMainWindow::setupTray()
{
    if (!QSystemTrayIcon::isSystemTrayAvailable()) {
        return;
    }
    m_tray = new QSystemTrayIcon(QIcon(QStringLiteral(":/QtScrcpy.ico")), this);
    auto *menu = new QMenu(this);
    menu->addAction(tr("Show"), this, [this]() {
        showNormal();
        raise();
        activateWindow();
    });
    menu->addAction(tr("Scan LAN now"), this, []() { DeviceDiscoveryService::instance().fullScan(); });
    menu->addSeparator();
    menu->addAction(tr("Quit"), this, [this]() { close(); });
    m_tray->setContextMenu(menu);
    connect(m_tray, &QSystemTrayIcon::activated, this, [this](QSystemTrayIcon::ActivationReason r) {
        if (r == QSystemTrayIcon::Trigger || r == QSystemTrayIcon::DoubleClick) {
            showNormal();
            raise();
            activateWindow();
        }
    });
    m_tray->show();
}

void FarmMainWindow::notify(const QString &title, const QString &message, bool warning)
{
    if (!FarmSettings::instance().notificationsEnabled() || !m_tray) {
        return;
    }
    m_tray->showMessage(title, message, warning ? QSystemTrayIcon::Warning : QSystemTrayIcon::Information, 5000);
}

void FarmMainWindow::closeEvent(QCloseEvent *event)
{
    FarmSettings::instance().setValue(QStringLiteral("ui/geometry"), saveGeometry());
    if (m_tray) {
        m_tray->hide();
    }
    event->accept();
}

void FarmMainWindow::keyPressEvent(QKeyEvent *event)
{
    QMainWindow::keyPressEvent(event);
}

} // namespace farm
