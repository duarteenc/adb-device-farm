#include "devicespage.h"

#include <algorithm>

#include <QDateTime>
#include <QIcon>
#include <QPixmap>
#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QCoreApplication>
#include <QCursor>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QImage>
#include <QInputDialog>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QScrollArea>
#include <QSlider>
#include <QSplitter>
#include <QStackedWidget>
#include <QTableWidget>
#include <QVBoxLayout>
#include <QWheelEvent>

#include <lunasvg.h>

#include "adb/adbexecutor.h"
#include "core/activitylog.h"
#include "core/appcontext.h"
#include "core/farmsettings.h"
#include "core/ipv4.h"
#include "devices/devicecommands.h"
#include "devices/deviceregistry.h"
#include "devices/deviceservice.h"
#include "devices/keepawakemanager.h"
#include "discovery/devicediscoveryservice.h"
#include "mock/mockdeviceprovider.h"
#include "ui/cursorbadge.h"
#include "ui/devicegrid.h"
#include "ui/devicetile.h"
#include "ui/farmtheme.h"
#include "ui/focuspanel.h"
#include "ui/widgets/batchjobdialog.h"
#include "ui/widgets/textsenddialog.h"

namespace farm {

namespace {
const char *kHelperPackage = "com.farmer333.wallpaperhelper";
}

DevicesPage::DevicesPage(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("page"));
    m_grid = new DeviceGrid(this);
    m_focus = new FocusPanel(this);
    m_focus->hide();
    m_badge = new CursorBadge();
    m_badge->hide();
    buildListView();

    m_viewStack = new QStackedWidget(this);
    m_viewStack->addWidget(m_grid);
    m_viewStack->addWidget(m_list);

    auto *center = new QWidget(this);
    auto *centerLay = new QHBoxLayout(center);
    centerLay->setContentsMargins(0, 0, 0, 0);
    centerLay->setSpacing(0);
    centerLay->addWidget(m_focus, 0);
    centerLay->addWidget(m_viewStack, 1);

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);
    root->addWidget(buildToolbar(), 0);
    auto *body = new QHBoxLayout();
    body->setContentsMargins(0, 0, 0, 0);
    body->setSpacing(0);
    body->addWidget(buildSidePanel(), 0);
    body->addWidget(center, 1);
    root->addLayout(body, 1);

    // ---- data wiring ----
    DeviceRegistry &registry = DeviceRegistry::instance();
    connect(&registry, &DeviceRegistry::deviceAdded, this, &DevicesPage::onDeviceAdded);
    connect(&registry, &DeviceRegistry::deviceChanged, this, &DevicesPage::onDeviceChanged);
    connect(&registry, &DeviceRegistry::deviceRemoved, this, &DevicesPage::onDeviceRemoved);
    connect(&registry, &DeviceRegistry::stateChanged, this, [this](const QString &, DeviceState, DeviceState) {
        m_orderTimer.start();
        refreshCounts();
    });
    connect(&registry, &DeviceRegistry::groupsChanged, this, [this]() {
        refreshGroupsList();
        m_orderTimer.start();
    });
    DeviceService &service = DeviceService::instance();
    connect(&service, &DeviceService::mirrorStarted, this, &DevicesPage::onMirrorStarted);
    connect(&service, &DeviceService::mirrorStopped, this, &DevicesPage::onMirrorStopped);
    // Mock mode: streams that Auto Mirror started before the page existed need their tiles attached.
    connect(&AppContext::instance(), &AppContext::servicesReady, this, [this]() {
        if (AppContext::instance().isMock()) {
            for (const QString &id : DeviceRegistry::instance().idsInState(DeviceState::Mirroring)) {
                onMirrorStarted(id, QSize());
            }
        }
    });
    connect(&service, &DeviceService::queueChanged, this, &DevicesPage::refreshCounts);
    connect(&DeviceDiscoveryService::instance(), &DeviceDiscoveryService::statusMessage, this, [this](const QString &t) {
        m_statusLabel->setText(t);
        emit statusMessage(t);
    });

    connect(m_grid, &DeviceGrid::selectionChanged, this, [this]() {
        const int n = static_cast<int>(m_grid->selectionSet().size());
        m_selectionLabel->setText(n == 0 ? tr("Nothing selected") : tr("%n device(s) selected", nullptr, n));
        updateHostFollowers();
        emit selectionChanged(n);
    });
    connect(m_grid, &DeviceGrid::tileDoubleClicked, this, &DevicesPage::onTileDoubleClicked);
    connect(m_grid, &DeviceGrid::contextMenuRequested, this, &DevicesPage::showContextMenu);
    connect(m_grid, &DeviceGrid::tileMouse, this, &DevicesPage::onTileMouse);
    connect(m_grid, &DeviceGrid::tileWheel, this, &DevicesPage::onTileWheel);
    connect(m_grid, &DeviceGrid::tileKey, this, &DevicesPage::onTileKey);
    connect(m_grid, &DeviceGrid::replayRequested, this, [this](const QString &id) {
        if (DeviceTile *t = m_grid->tile(id)) {
            if (auto d = DeviceService::instance().device(id)) {
                d->replayLastFrame(t);
            }
        }
    });
    connect(m_grid, &DeviceGrid::filesDropped, this, [this](const QString &id, const QStringList &files) {
        QStringList targets = id.isEmpty() ? selectedIds() : targetsFor(id);
        if (targets.isEmpty()) {
            targets = DeviceRegistry::instance().onlineIds();
        }
        QStringList apks;
        QStringList others;
        for (const QString &f : files) {
            (f.endsWith(QLatin1String(".apk"), Qt::CaseInsensitive) ? apks : others) << f;
        }
        for (const QString &apk : apks) {
            actInstallApk(targets, apk);
        }
        if (!others.isEmpty()) {
            actUploadFile(targets, others);
        }
    });

    connect(m_focus, &FocusPanel::closed, this, [this](const QString &) { closeHost(); });
    connect(m_focus, &FocusPanel::consoleRequested, this, [this](const QString &id) { emit consoleRequested(targetsFor(id)); });
    connect(m_focus, &FocusPanel::installApkRequested, this, [this](const QString &id) { actInstallApk(targetsFor(id)); });
    connect(m_focus, &FocusPanel::screenshotRequested, this, [this](const QStringList &ids) { actScreenshot(ids); });
    connect(m_focus, &FocusPanel::templatesRequested, this, [this]() { actSendText(targetsFor(m_focusId)); });

    // Carry badge follows the cursor while there is a selection (GenFarmer-style chip).
    m_badgeTimer.setInterval(40);
    connect(&m_badgeTimer, &QTimer::timeout, this, [this]() {
        int count = static_cast<int>(m_grid->selectionSet().size());
        const bool active = QGuiApplication::applicationState() == Qt::ApplicationActive;
        const QWidget *top = window();
        const bool over = top && top->frameGeometry().contains(QCursor::pos()) && isVisible();
        if (!active || !over || count <= 0) {
            if (m_badge->isVisible()) {
                m_badge->hide();
            }
            return;
        }
        m_badge->setCount(count);
        m_badge->moveToCursor(QCursor::pos());
        if (!m_badge->isVisible()) {
            m_badge->show();
            m_badge->raise();
        }
    });
    m_badgeTimer.start();

    m_orderTimer.setSingleShot(true);
    m_orderTimer.setInterval(50);
    connect(&m_orderTimer, &QTimer::timeout, this, &DevicesPage::refreshOrder);

    // Existing devices (registry loaded before the UI).
    const QStringList ids = registry.ids();
    for (const QString &id : ids) {
        onDeviceAdded(id);
    }
    refreshGroupsList();
    refreshOrder();
    refreshCounts();
}

DevicesPage::~DevicesPage()
{
    delete m_badge;
}

// ---------------------------------------------------------------- UI construction

QWidget *DevicesPage::buildToolbar()
{
    auto *bar = new QWidget(this);
    bar->setObjectName(QStringLiteral("topBar"));
    auto *lay = new QHBoxLayout(bar);
    lay->setContentsMargins(12, 8, 12, 8);
    lay->setSpacing(8);

    m_searchEdit = new QLineEdit(bar);
    m_searchEdit->setPlaceholderText(tr("Search number, name, serial, IP, model, group…"));
    m_searchEdit->setClearButtonEnabled(true);
    m_searchEdit->setMinimumWidth(240);
    connect(m_searchEdit, &QLineEdit::textChanged, this, [this](const QString &t) {
        m_search = t;
        m_orderTimer.start();
    });

    m_filterCombo = new QComboBox(bar);
    m_filterCombo->addItem(tr("All devices"), QStringLiteral("all"));
    m_filterCombo->addItem(tr("Online"), QStringLiteral("online"));
    m_filterCombo->addItem(tr("Offline"), QStringLiteral("offline"));
    m_filterCombo->addItem(tr("Mirroring"), QStringLiteral("mirroring"));
    m_filterCombo->addItem(tr("Favorites"), QStringLiteral("favorites"));
    m_filterCombo->addItem(tr("Recently offline"), QStringLiteral("recentOffline"));
    m_filterCombo->addItem(tr("Recently connected"), QStringLiteral("recentConnected"));
    m_filterCombo->addItem(tr("Automation running"), QStringLiteral("automation"));
    m_filterCombo->addItem(tr("Unauthorized / errors"), QStringLiteral("problems"));
    connect(m_filterCombo, &QComboBox::currentIndexChanged, this, [this](int) {
        m_filter = m_filterCombo->currentData().toString();
        m_orderTimer.start();
    });

    m_sortCombo = new QComboBox(bar);
    m_sortCombo->addItem(tr("Sort: number"), QStringLiteral("number"));
    m_sortCombo->addItem(tr("Sort: name"), QStringLiteral("name"));
    m_sortCombo->addItem(tr("Sort: IP"), QStringLiteral("ip"));
    m_sortCombo->addItem(tr("Sort: model"), QStringLiteral("model"));
    m_sortCombo->addItem(tr("Sort: battery"), QStringLiteral("battery"));
    m_sortCombo->addItem(tr("Sort: group"), QStringLiteral("group"));
    m_sortCombo->addItem(tr("Sort: online first"), QStringLiteral("online"));
    m_sortCombo->addItem(tr("Sort: automation"), QStringLiteral("automation"));
    m_sortCombo->addItem(tr("Sort: latency"), QStringLiteral("latency"));
    connect(m_sortCombo, &QComboBox::currentIndexChanged, this, [this](int) {
        m_sortKey = m_sortCombo->currentData().toString();
        m_orderTimer.start();
    });

    m_densityCombo = new QComboBox(bar);
    m_densityCombo->addItem(tr("Tiny"), QStringLiteral("tiny"));
    m_densityCombo->addItem(tr("Compact"), QStringLiteral("compact"));
    m_densityCombo->addItem(tr("Normal"), QStringLiteral("normal"));
    m_densityCombo->addItem(tr("Large"), QStringLiteral("large"));
    m_densityCombo->setCurrentIndex(std::max(0, m_densityCombo->findData(FarmSettings::instance().tileDensity())));
    connect(m_densityCombo, &QComboBox::currentIndexChanged, this, [this](int) {
        const QString d = m_densityCombo->currentData().toString();
        FarmSettings::instance().setValue(QStringLiteral("ui/tileDensity"), d);
        DeviceTile::Density density = DeviceTile::Density::Normal;
        int width = 190;
        if (d == QLatin1String("tiny")) {
            density = DeviceTile::Density::Tiny;
            width = 80;
        } else if (d == QLatin1String("compact")) {
            density = DeviceTile::Density::Compact;
            width = 130;
        } else if (d == QLatin1String("large")) {
            density = DeviceTile::Density::Large;
            width = 280;
        }
        m_grid->setDensity(density);
        m_tileSlider->setValue(width);
    });

    m_viewToggle = theme::button(tr("List view"), bar);
    m_viewToggle->setCheckable(true);
    connect(m_viewToggle, &QPushButton::toggled, this, [this](bool list) {
        m_viewStack->setCurrentIndex(list ? 1 : 0);
        m_viewToggle->setText(list ? tr("Grid view") : tr("List view"));
        if (list) {
            refreshOrder();
        }
    });

    auto *scanBtn = theme::button(tr("Scan LAN"), bar);
    scanBtn->setToolTip(tr("Probe the configured subnet for ADB devices now"));
    connect(scanBtn, &QPushButton::clicked, this, []() {
        DeviceDiscoveryService::instance().quickRefresh();
        DeviceDiscoveryService::instance().fullScan();
    });
    auto *mirrorAllBtn = theme::button(tr("Mirror all"), bar, QStringLiteral("primary"));
    connect(mirrorAllBtn, &QPushButton::clicked, this, [this]() {
        if (AppContext::instance().isMock()) {
            for (const QString &id : DeviceRegistry::instance().onlineIds()) {
                MockDeviceProvider::instance().setStreaming(id, true);
                onMirrorStarted(id, QSize());
            }
            return;
        }
        DeviceService::instance().startMirrorAll();
    });
    auto *stopAllBtn = theme::button(tr("Stop all"), bar);
    connect(stopAllBtn, &QPushButton::clicked, this, [this]() {
        if (AppContext::instance().isMock()) {
            for (const QString &id : DeviceRegistry::instance().ids()) {
                MockDeviceProvider::instance().setStreaming(id, false);
                onMirrorStopped(id);
            }
            return;
        }
        DeviceService::instance().stopMirrorAll();
    });
    m_countLabel = new QLabel(bar);
    m_countLabel->setObjectName(QStringLiteral("hint"));

    lay->addWidget(m_searchEdit, 1);
    lay->addWidget(m_filterCombo);
    lay->addWidget(m_sortCombo);
    lay->addWidget(m_densityCombo);
    lay->addWidget(m_viewToggle);
    lay->addWidget(scanBtn);
    lay->addWidget(mirrorAllBtn);
    lay->addWidget(stopAllBtn);
    lay->addWidget(m_countLabel);
    return bar;
}

QWidget *DevicesPage::buildSidePanel()
{
    auto *panel = new QWidget(this);
    panel->setObjectName(QStringLiteral("sidePanel"));
    panel->setFixedWidth(250);
    auto *col = new QVBoxLayout(panel);
    col->setContentsMargins(10, 10, 10, 10);
    col->setSpacing(6);

    m_selectionLabel = new QLabel(tr("Nothing selected"), panel);
    m_selectionLabel->setStyleSheet(QStringLiteral("font-weight:bold;"));
    col->addWidget(m_selectionLabel);

    auto selRow = [&](const QString &a, const QString &b, std::function<void()> fa, std::function<void()> fb) {
        auto *row = new QHBoxLayout();
        auto *ba = theme::button(a, panel);
        auto *bb = theme::button(b, panel);
        connect(ba, &QPushButton::clicked, this, fa);
        connect(bb, &QPushButton::clicked, this, fb);
        row->addWidget(ba);
        row->addWidget(bb);
        col->addLayout(row);
    };
    selRow(tr("Select all"), tr("Clear"), [this]() { m_grid->selectAll(); }, [this]() { m_grid->clearSelection(); });
    selRow(tr("Invert"), tr("Online"), [this]() { m_grid->invertSelection(); },
           [this]() {
               const QStringList on = DeviceRegistry::instance().onlineIds();
               m_grid->setSelection(QSet<QString>(on.begin(), on.end()));
           });
    auto *moreSel = theme::button(tr("Select by…"), panel);
    connect(moreSel, &QPushButton::clicked, this, [this, moreSel]() {
        QMenu menu(this);
        menu.addAction(tr("Offline devices"), this, [this]() {
            QSet<QString> s;
            for (const DeviceRecord &r : DeviceRegistry::instance().all()) {
                if (!r.isOnline()) {
                    s.insert(r.id);
                }
            }
            m_grid->setSelection(s);
        });
        menu.addAction(tr("Mirroring devices"), this, [this]() {
            const QStringList m = DeviceService::instance().mirroringIds();
            m_grid->setSelection(QSet<QString>(m.begin(), m.end()));
        });
        QMenu *models = menu.addMenu(tr("Model"));
        QMenu *versions = menu.addMenu(tr("Android version"));
        QSet<QString> seenModels;
        QSet<QString> seenVersions;
        for (const DeviceRecord &r : DeviceRegistry::instance().all()) {
            if (!r.model.isEmpty() && !seenModels.contains(r.model)) {
                seenModels.insert(r.model);
                const QString m = r.model;
                models->addAction(m, this, [this, m]() {
                    const QStringList ids = DeviceRegistry::instance().byModel(m);
                    m_grid->setSelection(QSet<QString>(ids.begin(), ids.end()));
                });
            }
            if (!r.androidVersion.isEmpty() && !seenVersions.contains(r.androidVersion)) {
                seenVersions.insert(r.androidVersion);
                const QString v = r.androidVersion;
                versions->addAction(QStringLiteral("Android %1").arg(v), this, [this, v]() {
                    const QStringList ids = DeviceRegistry::instance().byAndroidVersion(v);
                    m_grid->setSelection(QSet<QString>(ids.begin(), ids.end()));
                });
            }
        }
        QMenu *groups = menu.addMenu(tr("Group"));
        for (const GroupInfo &g : DeviceRegistry::instance().groups()) {
            const QString name = g.name;
            groups->addAction(name, this, [this, name]() {
                const QStringList ids = DeviceRegistry::instance().membersOf(name);
                m_grid->setSelection(QSet<QString>(ids.begin(), ids.end()));
            });
        }
        menu.exec(moreSel->mapToGlobal(QPoint(0, moreSel->height())));
    });
    col->addWidget(moreSel);

    col->addWidget(theme::sectionTitle(tr("Actions on selection"), panel));
    auto action = [&](const QString &label, std::function<void(const QStringList &)> fn, const QString &role = QString()) {
        auto *b = theme::button(label, panel, role);
        connect(b, &QPushButton::clicked, this, [this, fn]() {
            const QStringList ids = selectedIds();
            if (ids.isEmpty()) {
                m_statusLabel->setText(tr("Select devices first."));
                return;
            }
            fn(ids);
        });
        col->addWidget(b);
        return b;
    };
    action(tr("Mirror selected"), [this](const QStringList &ids) { actMirror(ids); }, QStringLiteral("primary"));
    action(tr("Stop mirror"), [this](const QStringList &ids) { actStop(ids); });
    action(tr("Screenshot"), [this](const QStringList &ids) { actScreenshot(ids); });
    action(tr("Send text…"), [this](const QStringList &ids) { actSendText(ids); });
    action(tr("Install APK…"), [this](const QStringList &ids) { actInstallApk(ids); });
    action(tr("Upload file…"), [this](const QStringList &ids) { actUploadFile(ids); });
    action(tr("ADB console"), [this](const QStringList &ids) { emit consoleRequested(ids); });
    action(tr("Run automation…"), [this](const QStringList &ids) { emit automationRequested(ids); });

    col->addWidget(theme::sectionTitle(tr("Quality"), panel));
    m_presetCombo = new QComboBox(panel);
    m_presetCombo->addItem(tr("Performance (360p / 15 fps)"), QStringLiteral("performance"));
    m_presetCombo->addItem(tr("Balanced (720p / 30 fps)"), QStringLiteral("balanced"));
    m_presetCombo->addItem(tr("Quality (1080p / 60 fps)"), QStringLiteral("quality"));
    m_presetCombo->addItem(tr("Custom"), QStringLiteral("custom"));
    col->addWidget(m_presetCombo);
    auto makeSlider = [&](const QString &name, int lo, int hi, int value, QLabel **out) {
        auto *row = new QHBoxLayout();
        auto *label = new QLabel(name, panel);
        auto *val = new QLabel(QString::number(value), panel);
        val->setObjectName(QStringLiteral("hint"));
        val->setAlignment(Qt::AlignRight);
        row->addWidget(label);
        row->addWidget(val);
        col->addLayout(row);
        auto *s = new QSlider(Qt::Horizontal, panel);
        s->setRange(lo, hi);
        s->setValue(value);
        col->addWidget(s);
        *out = val;
        return s;
    };
    const MirrorProfile gp = DeviceService::instance().globalProfile();
    m_sizeSlider = makeSlider(tr("Resolution (px)"), 240, 1440, gp.maxSize, &m_sizeValue);
    m_fpsSlider = makeSlider(tr("Frame rate"), 5, 60, gp.maxFps, &m_fpsValue);
    m_bitrateSlider = makeSlider(tr("Bitrate (Mbps)"), 1, 20, std::max(1, gp.bitRate / 1000000), &m_bitrateValue);
    m_presetCombo->setCurrentIndex(std::max(0, m_presetCombo->findData(gp.name.isEmpty() ? QStringLiteral("custom") : gp.name)));
    connect(m_presetCombo, &QComboBox::currentIndexChanged, this, [this](int) { applyPreset(m_presetCombo->currentData().toString()); });
    connect(m_sizeSlider, &QSlider::valueChanged, this, [this](int v) { m_sizeValue->setText(QString::number(v)); });
    connect(m_fpsSlider, &QSlider::valueChanged, this, [this](int v) { m_fpsValue->setText(QString::number(v)); });
    connect(m_bitrateSlider, &QSlider::valueChanged, this, [this](int v) { m_bitrateValue->setText(QString::number(v)); });
    for (QSlider *s : { m_sizeSlider, m_fpsSlider, m_bitrateSlider }) {
        connect(s, &QSlider::sliderReleased, this, [this]() {
            m_presetCombo->setCurrentIndex(m_presetCombo->findData(QStringLiteral("custom")));
            applyCustomProfile();
        });
    }
    auto *applyRunning = theme::button(tr("Apply to running mirrors"), panel);
    connect(applyRunning, &QPushButton::clicked, this, [this]() {
        applyCustomProfile();
        DeviceService::instance().setGlobalProfile(DeviceService::instance().globalProfile(), true);
    });
    col->addWidget(applyRunning);
    auto *adaptive = new QCheckBox(tr("Adaptive quality (auto-lower with many mirrors)"), panel);
    adaptive->setChecked(FarmSettings::instance().adaptiveQuality());
    connect(adaptive, &QCheckBox::toggled, this, [](bool on) { FarmSettings::instance().setValue(QStringLiteral("mirror/adaptiveQuality"), on); });
    col->addWidget(adaptive);

    col->addWidget(theme::sectionTitle(tr("Layout"), panel));
    QLabel *tileVal = nullptr;
    m_tileSlider = makeSlider(tr("Tile size"), 70, 400, FarmSettings::instance().tileWidth(), &tileVal);
    connect(m_tileSlider, &QSlider::valueChanged, this, [this, tileVal](int v) {
        tileVal->setText(QString::number(v));
        m_grid->setTileWidth(v);
        FarmSettings::instance().setValue(QStringLiteral("ui/tileWidth"), v);
    });
    m_grid->setTileWidth(m_tileSlider->value());
    QLabel *hostVal = nullptr;
    m_hostSlider = makeSlider(tr("Host screen height"), 400, 1400, 720, &hostVal);
    connect(m_hostSlider, &QSlider::valueChanged, this, [this, hostVal](int v) {
        hostVal->setText(QString::number(v));
        m_focus->setHostHeight(v);
    });
    m_smallCtrlCheck = new QCheckBox(tr("Control in small view"), panel);
    m_smallCtrlCheck->setToolTip(tr("On: tap/drag a grid tile controls that phone.\nOff: the grid is for selection only."));
    connect(m_smallCtrlCheck, &QCheckBox::toggled, this, [this](bool on) {
        m_smallViewControl = on;
        m_grid->setControllable(on);
    });
    m_controlAllCheck = new QCheckBox(tr("Control All (broadcast to every mirror)"), panel);
    connect(m_controlAllCheck, &QCheckBox::toggled, this, [this](bool on) {
        m_controlAll = on;
        updateHostFollowers();
    });
    col->addWidget(m_smallCtrlCheck);
    col->addWidget(m_controlAllCheck);

    col->addWidget(theme::sectionTitle(tr("Groups"), panel));
    m_groupsList = new QListWidget(panel);
    m_groupsList->setFixedHeight(120);
    m_groupsList->setToolTip(tr("Click: select members · Double-click: show only this group"));
    connect(m_groupsList, &QListWidget::itemClicked, this, [this](QListWidgetItem *item) {
        const QStringList ids = DeviceRegistry::instance().membersOf(item->data(Qt::UserRole).toString());
        m_grid->setSelection(QSet<QString>(ids.begin(), ids.end()));
    });
    connect(m_groupsList, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem *item) {
        const QString g = item->data(Qt::UserRole).toString();
        setGroupFilter(m_groupFilter == g ? QString() : g);
    });
    col->addWidget(m_groupsList);

    col->addWidget(theme::sectionTitle(tr("WiFi connect"), panel));
    m_wifiRange = new QLineEdit(panel);
    m_wifiRange->setPlaceholderText(tr("192.168.100.1-254 or 192.168.100.0/24"));
    const QString subnet = FarmSettings::instance().subnet();
    m_wifiRange->setText(subnet);
    auto *wifiRow = new QHBoxLayout();
    m_wifiPort = new QLineEdit(QString::number(FarmSettings::instance().adbPort()), panel);
    m_wifiPort->setFixedWidth(60);
    auto *connectBtn = theme::button(tr("Connect range"), panel, QStringLiteral("primary"));
    wifiRow->addWidget(m_wifiPort);
    wifiRow->addWidget(connectBtn, 1);
    col->addWidget(m_wifiRange);
    col->addLayout(wifiRow);
    connect(connectBtn, &QPushButton::clicked, this, &DevicesPage::connectWifiRange);
    connect(m_wifiRange, &QLineEdit::returnPressed, this, &DevicesPage::connectWifiRange);
    auto *enableWifi = theme::button(tr("USB → WiFi ADB (selected)"), panel);
    enableWifi->setToolTip(tr("adb tcpip 5555 on selected USB devices, read their WiFi IP and connect"));
    connect(enableWifi, &QPushButton::clicked, this, &DevicesPage::enableWifiOnSelected);
    col->addWidget(enableWifi);

    col->addStretch(1);
    m_statusLabel = theme::hint(tr("Ready."), panel);
    col->addWidget(m_statusLabel);

    auto *scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setWidget(panel);
    scroll->setFixedWidth(266);
    return scroll;
}

void DevicesPage::buildListView()
{
    m_list = new QTableWidget(0, 10, this);
    m_list->setHorizontalHeaderLabels({ tr("#"), tr("Name"), tr("Model"), tr("ID / IP"), tr("State"), tr("Battery"), tr("Group"), tr("Android"), tr("Latency"), tr("Keep awake") });
    m_list->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    m_list->horizontalHeader()->setStretchLastSection(true);
    m_list->verticalHeader()->setVisible(false);
    m_list->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_list->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_list->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_list->setAlternatingRowColors(true);
    m_list->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_list, &QTableWidget::itemSelectionChanged, this, [this]() {
        QSet<QString> sel;
        const QList<QTableWidgetSelectionRange> ranges = m_list->selectedRanges();
        for (const QTableWidgetSelectionRange &r : ranges) {
            for (int row = r.topRow(); row <= r.bottomRow(); ++row) {
                if (QTableWidgetItem *it = m_list->item(row, 0)) {
                    sel.insert(it->data(Qt::UserRole).toString());
                }
            }
        }
        if (sel != m_grid->selectionSet()) {
            m_grid->setSelection(sel);
        }
    });
    connect(m_list, &QTableWidget::customContextMenuRequested, this, [this](const QPoint &pos) {
        QTableWidgetItem *it = m_list->itemAt(pos);
        showContextMenu(it ? m_list->item(it->row(), 0)->data(Qt::UserRole).toString() : QString(), m_list->viewport()->mapToGlobal(pos));
    });
    connect(m_list, &QTableWidget::itemDoubleClicked, this, [this](QTableWidgetItem *it) {
        onTileDoubleClicked(m_list->item(it->row(), 0)->data(Qt::UserRole).toString());
        m_viewToggle->setChecked(false);
    });
}

// ---------------------------------------------------------------- data

void DevicesPage::onDeviceAdded(const QString &id)
{
    DeviceTile *tile = m_grid->ensureTile(id);
    tile->setRecord(DeviceRegistry::instance().get(id));
    m_orderTimer.start();
    refreshCounts();
}

void DevicesPage::onDeviceChanged(const QString &id)
{
    if (DeviceTile *tile = m_grid->tile(id)) {
        DeviceRecord r = DeviceRegistry::instance().get(id);
        if (!r.group.isEmpty()) {
            r.props.insert(QStringLiteral("_groupColor"), DeviceRegistry::instance().group(r.group).color);
        }
        tile->setRecord(r);
    }
    if (m_viewStack->currentIndex() == 1) {
        refreshListRow(id);
    }
    if (id == m_focusId) {
        const DeviceRecord r = DeviceRegistry::instance().get(id);
        m_focus->showDevice(id, QStringLiteral("%1 %2").arg(r.numberString(), r.displayName()));
    }
}

void DevicesPage::onDeviceRemoved(const QString &id)
{
    if (id == m_focusId) {
        closeHost();
    }
    m_grid->removeTile(id);
    m_orderTimer.start();
    refreshCounts();
}

void DevicesPage::attachTile(const QString &id)
{
    DeviceTile *tile = m_grid->ensureTile(id);
    if (MockDeviceProvider::isMockId(id)) {
        MockDeviceProvider::instance().attach(id, tile);
    } else if (auto d = DeviceService::instance().device(id)) {
        d->registerDeviceObserver(tile);
        d->replayLastFrame(tile);
    }
    tile->setStreaming(true);
}

void DevicesPage::detachTile(const QString &id)
{
    DeviceTile *tile = m_grid->tile(id);
    if (!tile) {
        return;
    }
    if (MockDeviceProvider::isMockId(id)) {
        MockDeviceProvider::instance().detach(id, tile);
    } else if (auto d = DeviceService::instance().device(id)) {
        d->deRegisterDeviceObserver(tile);
    }
    tile->setStreaming(false);
}

void DevicesPage::onMirrorStarted(const QString &id, const QSize &)
{
    attachTile(id);
    if (!MockDeviceProvider::isMockId(id)) {
        ensureHelperApk(id);
    }
    refreshCounts();
}

void DevicesPage::onMirrorStopped(const QString &id)
{
    detachTile(id);
    if (id == m_focusId) {
        closeHost();
    }
    refreshCounts();
}

void DevicesPage::refreshOrder()
{
    DeviceRegistry &registry = DeviceRegistry::instance();
    QStringList ids = registry.ids();
    // filter
    QStringList filtered;
    const QStringList mirroring = DeviceService::instance().mirroringIds();
    for (const QString &id : ids) {
        const DeviceRecord r = registry.get(id);
        bool keep = true;
        if (m_filter == QLatin1String("online")) {
            keep = r.isOnline();
        } else if (m_filter == QLatin1String("offline")) {
            keep = !r.isOnline();
        } else if (m_filter == QLatin1String("mirroring")) {
            keep = r.state == DeviceState::Mirroring || mirroring.contains(id);
        } else if (m_filter == QLatin1String("favorites")) {
            keep = r.favorite;
        } else if (m_filter == QLatin1String("recentOffline")) {
            keep = !r.isOnline() && r.lastStateChange.isValid() && r.lastStateChange.secsTo(QDateTime::currentDateTime()) < 1800;
        } else if (m_filter == QLatin1String("recentConnected")) {
            keep = r.isOnline() && r.lastStateChange.isValid() && r.lastStateChange.secsTo(QDateTime::currentDateTime()) < 1800;
        } else if (m_filter == QLatin1String("automation")) {
            keep = r.automationRunning;
        } else if (m_filter == QLatin1String("problems")) {
            keep = r.state == DeviceState::Unauthorized || r.state == DeviceState::Error || r.keepAwakeStatus.startsWith(QLatin1String("Failed"));
        }
        if (keep && !m_groupFilter.isEmpty()) {
            keep = r.group == m_groupFilter;
        }
        if (keep) {
            filtered << id;
        }
    }
    if (!m_search.isEmpty()) {
        filtered = registry.search(m_search, filtered);
    }
    DeviceRegistry::SortKey key = DeviceRegistry::SortKey::Number;
    if (m_sortKey == QLatin1String("name")) {
        key = DeviceRegistry::SortKey::Name;
    } else if (m_sortKey == QLatin1String("ip")) {
        key = DeviceRegistry::SortKey::Ip;
    } else if (m_sortKey == QLatin1String("model")) {
        key = DeviceRegistry::SortKey::Model;
    } else if (m_sortKey == QLatin1String("battery")) {
        key = DeviceRegistry::SortKey::Battery;
    } else if (m_sortKey == QLatin1String("group")) {
        key = DeviceRegistry::SortKey::Group;
    } else if (m_sortKey == QLatin1String("online")) {
        key = DeviceRegistry::SortKey::Online;
    } else if (m_sortKey == QLatin1String("automation")) {
        key = DeviceRegistry::SortKey::Automation;
    } else if (m_sortKey == QLatin1String("latency")) {
        key = DeviceRegistry::SortKey::Latency;
    }
    const QStringList ordered = registry.sorted(key, true, filtered);
    m_grid->setOrder(ordered);

    if (m_viewStack->currentIndex() == 1) {
        m_list->setRowCount(static_cast<int>(ordered.size()));
        m_listRows.clear();
        for (int i = 0; i < ordered.size(); ++i) {
            m_listRows.insert(ordered.at(i), i);
            for (int c = 0; c < m_list->columnCount(); ++c) {
                if (!m_list->item(i, c)) {
                    m_list->setItem(i, c, new QTableWidgetItem());
                }
            }
            m_list->item(i, 0)->setData(Qt::UserRole, ordered.at(i));
            refreshListRow(ordered.at(i));
        }
    }
    refreshCounts();
}

void DevicesPage::refreshListRow(const QString &id)
{
    if (!m_listRows.contains(id)) {
        return;
    }
    const int row = m_listRows.value(id);
    const DeviceRecord r = DeviceRegistry::instance().get(id);
    m_list->item(row, 0)->setText(r.numberString());
    m_list->item(row, 1)->setText(r.displayName());
    m_list->item(row, 2)->setText(r.model);
    m_list->item(row, 3)->setText(r.id);
    m_list->item(row, 4)->setText(deviceStateName(r.state) + (r.stateMessage.isEmpty() ? QString() : QStringLiteral(" · ") + r.stateMessage));
    m_list->item(row, 4)->setForeground(theme::stateColor(static_cast<int>(r.state)));
    m_list->item(row, 5)->setText(r.battery >= 0 ? QStringLiteral("%1%%2").arg(r.battery).arg(r.charging ? QStringLiteral(" ⚡") : QString()) : QString());
    m_list->item(row, 6)->setText(r.group);
    m_list->item(row, 7)->setText(r.androidVersion.isEmpty() ? QString() : QStringLiteral("%1 (SDK %2)").arg(r.androidVersion).arg(r.sdk));
    m_list->item(row, 8)->setText(r.latencyMs >= 0 ? QStringLiteral("%1 ms").arg(r.latencyMs) : QString());
    m_list->item(row, 9)->setText(r.keepAwakeStatus);
}

void DevicesPage::refreshCounts()
{
    const DeviceRegistry &registry = DeviceRegistry::instance();
    const int known = registry.count();
    const int online = static_cast<int>(registry.onlineIds().size());
    const int mirroring = AppContext::instance().isMock() ? registry.countInState(DeviceState::Mirroring) : DeviceService::instance().mirroringCount();
    const int shown = static_cast<int>(m_grid->order().size());
    m_countLabel->setText(tr("%1 shown · %2 online · %3 mirroring · %4 known").arg(shown).arg(online).arg(mirroring).arg(known));
}

void DevicesPage::refreshGroupsList()
{
    m_groupsList->clear();
    for (const GroupInfo &g : DeviceRegistry::instance().groups()) {
        const int members = static_cast<int>(DeviceRegistry::instance().membersOf(g.name).size());
        auto *item = new QListWidgetItem(QStringLiteral("%1  (%2)").arg(g.name).arg(members), m_groupsList);
        item->setData(Qt::UserRole, g.name);
        QPixmap pm(12, 12);
        pm.fill(QColor(g.color));
        item->setIcon(QIcon(pm));
        if (g.name == m_groupFilter) {
            item->setSelected(true);
        }
    }
}

void DevicesPage::setGroupFilter(const QString &group)
{
    m_groupFilter = group;
    m_statusLabel->setText(group.isEmpty() ? tr("Showing all groups.") : tr("Showing only group '%1' (double-click again to clear).").arg(group));
    refreshGroupsList();
    refreshOrder();
}

QStringList DevicesPage::selectedIds() const
{
    return m_grid->selection();
}

void DevicesPage::selectIds(const QStringList &ids)
{
    m_grid->setSelection(QSet<QString>(ids.begin(), ids.end()));
}

void DevicesPage::setSearchQuery(const QString &query)
{
    m_searchEdit->setText(query);
}

void DevicesPage::focusDevice(const QString &id)
{
    m_grid->scrollToTile(id);
    if (DeviceTile *t = m_grid->tile(id)) {
        t->setHighlight(true);
        QTimer::singleShot(2500, t, [t]() { t->setHighlight(false); });
    }
}

// ---------------------------------------------------------------- host mode

void DevicesPage::onTileDoubleClicked(const QString &id)
{
    const bool live = AppContext::instance().isMock() ? MockDeviceProvider::instance().isStreaming(id) : DeviceService::instance().isMirroring(id);
    if (!live) {
        actMirror({ id });
        m_statusLabel->setText(tr("Starting mirror on %1 — double-click again for host mode.").arg(id));
        return;
    }
    if (m_focusId == id && m_focus->isVisible()) {
        closeHost();
        return;
    }
    openHost(id);
}

void DevicesPage::openHost(const QString &id)
{
    m_focusId = id;
    m_grid->setFocusedId(id);
    const DeviceRecord r = DeviceRegistry::instance().get(id);
    m_focus->setVisible(true);
    m_focus->showDevice(id, QStringLiteral("%1 %2").arg(r.numberString(), r.displayName()));
    updateHostFollowers();
}

void DevicesPage::closeHost()
{
    m_focus->detach();
    m_focus->hide();
    m_grid->setFocusedId(QString());
    m_focusId.clear();
}

void DevicesPage::updateHostFollowers()
{
    if (m_focusId.isEmpty()) {
        return;
    }
    QStringList followers;
    if (m_controlAll) {
        followers = AppContext::instance().isMock() ? DeviceRegistry::instance().idsInState(DeviceState::Mirroring) : DeviceService::instance().mirroringIds();
    } else {
        followers = selectedIds();
    }
    followers.removeAll(m_focusId);
    m_focus->setFollowers(followers);
}

QStringList DevicesPage::inputTargets(const QString &source) const
{
    if (m_controlAll) {
        return DeviceService::instance().mirroringIds();
    }
    const QSet<QString> sel = m_grid->selectionSet();
    if (sel.size() > 1 && sel.contains(source)) {
        return selectedIds();
    }
    return QStringList{ source };
}

void DevicesPage::onTileMouse(const QString &id, QMouseEvent *event)
{
    DeviceTile *src = m_grid->tile(id);
    if (!src) {
        return;
    }
    const QSize showSize = src->videoShowSize();
    const bool press = event->type() == QEvent::MouseButtonPress;
    for (const QString &target : inputTargets(id)) {
        auto device = DeviceService::instance().device(target);
        if (!device) {
            continue;
        }
        if (press && event->button() == Qt::RightButton) {
            device->postGoBack();
            continue;
        }
        if (press && event->button() == Qt::MiddleButton) {
            device->postGoHome();
            continue;
        }
        const QSize frame = DeviceService::instance().frameSize(target);
        device->mouseEvent(event, frame.isEmpty() ? src->videoFrameSize() : frame, showSize);
    }
}

void DevicesPage::onTileWheel(const QString &id, QWheelEvent *event)
{
    DeviceTile *src = m_grid->tile(id);
    if (!src) {
        return;
    }
    for (const QString &target : inputTargets(id)) {
        if (auto device = DeviceService::instance().device(target)) {
            const QSize frame = DeviceService::instance().frameSize(target);
            device->wheelEvent(event, frame.isEmpty() ? src->videoFrameSize() : frame, src->videoShowSize());
        }
    }
}

void DevicesPage::onTileKey(const QString &id, QKeyEvent *event)
{
    DeviceTile *src = m_grid->tile(id);
    if (!src) {
        return;
    }
    if ((event->modifiers() & Qt::ControlModifier) && event->key() == Qt::Key_V) {
        if (event->type() == QEvent::KeyPress) {
            for (const QString &target : inputTargets(id)) {
                if (auto device = DeviceService::instance().device(target)) {
                    device->setDeviceClipboard(true);
                }
            }
        }
        return;
    }
    for (const QString &target : inputTargets(id)) {
        if (auto device = DeviceService::instance().device(target)) {
            device->keyEvent(event, src->videoFrameSize(), src->videoShowSize());
        }
    }
}

// ---------------------------------------------------------------- context menu

QStringList DevicesPage::targetsFor(const QString &clicked) const
{
    const QSet<QString> sel = m_grid->selectionSet();
    if (!clicked.isEmpty() && (sel.isEmpty() || !sel.contains(clicked))) {
        return QStringList{ clicked };
    }
    return selectedIds();
}

bool DevicesPage::confirmBulk(const QString &action, int count, bool destructive)
{
    if (!destructive && count < 2) {
        return true;
    }
    if (!destructive && count < 10) {
        return true;
    }
    const QString text = tr("%1\n\nThis operation will affect %n device(s).", nullptr, count).arg(action);
    return QMessageBox::question(this, tr("Confirm"), text, QMessageBox::Yes | QMessageBox::No, QMessageBox::No) == QMessageBox::Yes;
}

void DevicesPage::buildGroupMenu(QMenu *menu, const QStringList &ids)
{
    QMenu *sub = menu->addMenu(tr("Group"));
    for (const GroupInfo &g : DeviceRegistry::instance().groups()) {
        bool allIn = !ids.isEmpty();
        for (const QString &id : ids) {
            if (DeviceRegistry::instance().get(id).group != g.name) {
                allIn = false;
                break;
            }
        }
        QAction *a = sub->addAction(g.name);
        a->setCheckable(true);
        a->setChecked(allIn);
        const QString name = g.name;
        connect(a, &QAction::triggered, this, [ids, name, allIn]() { DeviceRegistry::instance().assignGroup(ids, allIn ? QString() : name); });
    }
    if (!DeviceRegistry::instance().groups().isEmpty()) {
        sub->addSeparator();
    }
    sub->addAction(tr("New group…"), this, [this, ids]() {
        bool ok = false;
        const QString name = QInputDialog::getText(this, tr("New group"), tr("Group name:"), QLineEdit::Normal, QString(), &ok).trimmed();
        if (ok && !name.isEmpty()) {
            DeviceRegistry::instance().createGroup(name);
            DeviceRegistry::instance().assignGroup(ids, name);
        }
    });
    sub->addAction(tr("Remove from group"), this, [ids]() { DeviceRegistry::instance().assignGroup(ids, QString()); });
}

void DevicesPage::showContextMenu(const QString &clicked, const QPoint &globalPos)
{
    const QStringList ids = targetsFor(clicked);
    QMenu menu(this);
    if (ids.isEmpty()) {
        menu.addAction(tr("Select all"), this, [this]() { m_grid->selectAll(); });
        menu.addAction(tr("Scan LAN now"), this, []() { DeviceDiscoveryService::instance().fullScan(); });
        menu.addAction(tr("Mirror all"), this, []() { DeviceService::instance().startMirrorAll(); });
        menu.exec(globalPos);
        return;
    }
    const int n = static_cast<int>(ids.size());
    const QString suffix = n > 1 ? tr(" (%1 devices)").arg(n) : QString();
    const DeviceRecord first = DeviceRegistry::instance().get(ids.first());
    menu.addAction(tr("Mirror") + suffix, this, [this, ids]() { actMirror(ids); });
    menu.addAction(tr("Stop mirror") + suffix, this, [this, ids]() { actStop(ids); });
    menu.addAction(tr("Restart mirror") + suffix, this, [this, ids]() { actRestart(ids); });
    menu.addAction(tr("Reconnect") + suffix, this, [this, ids]() { actReconnect(ids); });
    if (n == 1) {
        menu.addAction(tr("Open host mode"), this, [this, ids]() { openHost(ids.first()); });
        menu.addAction(tr("Inspect…"), this, [this, ids]() { emit inspectRequested(ids.first()); });
    }
    menu.addSeparator();
    menu.addAction(tr("Screenshot") + suffix, this, [this, ids]() { actScreenshot(ids); });
    menu.addAction(tr("Record screen (30 s)") + suffix, this, [this, ids]() { actRecord(ids); });
    menu.addAction(tr("Send text…") + suffix, this, [this, ids]() { actSendText(ids); });
    menu.addAction(tr("Install APK…") + suffix, this, [this, ids]() { actInstallApk(ids); });
    menu.addAction(tr("Upload file…") + suffix, this, [this, ids]() { actUploadFile(ids); });
    menu.addAction(tr("Launch app…") + suffix, this, [this, ids]() { actLaunchApp(ids); });
    menu.addAction(tr("Applications…"), this, [this, ids]() { emit appsRequested(ids); });
    menu.addAction(tr("Files…"), this, [this, ids]() { emit filesRequested(ids); });
    menu.addAction(tr("ADB console") + suffix, this, [this, ids]() { emit consoleRequested(ids); });
    menu.addAction(tr("Run automation…") + suffix, this, [this, ids]() { emit automationRequested(ids); });
    menu.addSeparator();
    QMenu *power = menu.addMenu(tr("Keep awake / power"));
    power->addAction(tr("Apply keep-awake policy"), this, [this, ids]() { actKeepAwake(ids, true); });
    power->addAction(tr("Restore default timeout (30 s)"), this, [this, ids]() { actKeepAwake(ids, false); });
    power->addAction(tr("Wake screen"), this, [this, ids]() { actWake(ids); });
    power->addAction(tr("Reboot") + suffix, this, [this, ids]() { actReboot(ids); });
    QMenu *edit = menu.addMenu(tr("Edit"));
    if (n == 1) {
        edit->addAction(tr("Rename…"), this, [this, ids]() { actRename(ids.first()); });
    }
    edit->addAction(tr("Set number…"), this, [this, ids]() { actSetNumber(ids); });
    edit->addAction(first.favorite ? tr("Remove from favorites") : tr("Add to favorites"), this, [this, ids, first]() { actFavorite(ids, !first.favorite); });
    edit->addAction(tr("Set numbered wallpaper"), this, [this, ids]() { setNumberedWallpapers(ids); });
    edit->addAction(tr("Reset resolution/density override"), this, [this, ids]() { actNormalizeReset(ids); });
    buildGroupMenu(&menu, ids);
    menu.addSeparator();
    menu.addAction(tr("Disconnect") + suffix, this, [this, ids]() { actDisconnect(ids); });
    menu.addAction(tr("Forget device") + suffix, this, [this, ids]() { actForget(ids); });
    menu.exec(globalPos);
}

// ---------------------------------------------------------------- actions

void DevicesPage::actMirror(const QStringList &ids)
{
    if (AppContext::instance().isMock()) {
        for (const QString &id : ids) {
            MockDeviceProvider::instance().setStreaming(id, true);
            onMirrorStarted(id, QSize());
        }
        return;
    }
    DeviceService::instance().startMirror(ids);
    m_statusLabel->setText(tr("Mirror requested on %n device(s).", nullptr, static_cast<int>(ids.size())));
}

void DevicesPage::actStop(const QStringList &ids)
{
    if (AppContext::instance().isMock()) {
        for (const QString &id : ids) {
            MockDeviceProvider::instance().setStreaming(id, false);
            onMirrorStopped(id);
        }
        return;
    }
    DeviceService::instance().stopMirror(ids);
}

void DevicesPage::actRestart(const QStringList &ids)
{
    for (const QString &id : ids) {
        DeviceService::instance().restartMirror(id);
    }
}

void DevicesPage::actReconnect(const QStringList &ids)
{
    for (const QString &id : ids) {
        if (AppContext::instance().isMock()) {
            MockDeviceProvider::instance().simulateDisconnect(id, 3000);
        } else {
            DeviceService::instance().reconnectDevice(id);
        }
    }
}

void DevicesPage::actReboot(const QStringList &ids)
{
    if (!confirmBulk(tr("Reboot the selected devices?"), static_cast<int>(ids.size()), true)) {
        return;
    }
    BatchJobDialog::show(DeviceCommands::reboot(ids), this);
}

void DevicesPage::actScreenshot(const QStringList &ids)
{
    const QString dir = FarmSettings::instance().screenshotDirectory();
    BatchJob *job = DeviceCommands::screenshot(ids, dir);
    if (ids.size() > 1) {
        BatchJobDialog::show(job, this);
    } else {
        connect(job, &BatchJob::finished, this, [this, job](BatchJob::Status) {
            const BatchJob::Item it = job->items().value(0);
            m_statusLabel->setText(it.status == BatchJob::Succeeded ? tr("Saved %1").arg(it.message) : tr("Screenshot failed: %1").arg(it.message));
        });
    }
    m_statusLabel->setText(tr("Screenshots → %1").arg(QDir::toNativeSeparators(dir)));
}

void DevicesPage::actRecord(const QStringList &ids)
{
    bool ok = false;
    const int seconds = QInputDialog::getInt(this, tr("Record screen"), tr("Duration in seconds (max 180):"), 30, 1, 180, 5, &ok);
    if (!ok) {
        return;
    }
    BatchJobDialog::show(DeviceCommands::startRecording(ids, FarmSettings::instance().recordingDirectory(), seconds), this);
}

void DevicesPage::actSendText(const QStringList &ids)
{
    auto *dlg = new TextSendDialog(ids, this);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    connect(dlg, &TextSendDialog::sendRequested, this, [this](const QStringList &targets, const QString &text) {
        BatchJob *job = DeviceCommands::inputText(targets, text);
        if (targets.size() > 1) {
            BatchJobDialog::show(job, this);
        }
    });
    connect(dlg, &TextSendDialog::clipboardRequested, this, [this](const QStringList &targets, const QString &text) {
        QApplication::clipboard()->setText(text);
        BatchJobDialog::show(DeviceCommands::setClipboardText(targets, text, false), this);
    });
    dlg->show();
}

void DevicesPage::actInstallApk(const QStringList &ids, const QString &pathIn)
{
    QString path = pathIn;
    if (path.isEmpty()) {
        path = QFileDialog::getOpenFileName(this, tr("Choose APK"), FarmSettings::instance().stringValue(QStringLiteral("ui/lastApkDir"), QString()), tr("Android packages (*.apk)"));
        if (path.isEmpty()) {
            return;
        }
        FarmSettings::instance().setValue(QStringLiteral("ui/lastApkDir"), QFileInfo(path).absolutePath());
    }
    if (!confirmBulk(tr("Install %1?").arg(QFileInfo(path).fileName()), static_cast<int>(ids.size()), false)) {
        return;
    }
    BatchJobDialog::show(DeviceCommands::installApk(ids, path, true, false), this);
}

void DevicesPage::actUploadFile(const QStringList &ids, const QStringList &pathsIn)
{
    QStringList paths = pathsIn;
    if (paths.isEmpty()) {
        paths = QFileDialog::getOpenFileNames(this, tr("Choose files to upload"), FarmSettings::instance().stringValue(QStringLiteral("ui/lastUploadDir"), QString()));
        if (paths.isEmpty()) {
            return;
        }
        FarmSettings::instance().setValue(QStringLiteral("ui/lastUploadDir"), QFileInfo(paths.first()).absolutePath());
    }
    bool ok = false;
    const QString remote = QInputDialog::getItem(this, tr("Upload to"), tr("Device folder:"),
                                                 { QStringLiteral("/sdcard/Download"), QStringLiteral("/sdcard/DCIM"), QStringLiteral("/sdcard/Pictures"), QStringLiteral("/sdcard/Documents"), QStringLiteral("/sdcard/Movies") }, 0, true, &ok);
    if (!ok || remote.isEmpty()) {
        return;
    }
    BatchJobDialog::show(DeviceCommands::pushFiles(ids, paths, remote), this);
}

void DevicesPage::actLaunchApp(const QStringList &ids)
{
    bool ok = false;
    const QString pkg = QInputDialog::getText(this, tr("Launch app"), tr("Package name:"), QLineEdit::Normal,
                                              FarmSettings::instance().stringValue(QStringLiteral("ui/lastPackage"), QStringLiteral("com.android.settings")), &ok).trimmed();
    if (!ok || pkg.isEmpty()) {
        return;
    }
    FarmSettings::instance().setValue(QStringLiteral("ui/lastPackage"), pkg);
    BatchJobDialog::show(DeviceCommands::launchApp(ids, pkg), this);
}

void DevicesPage::actKeepAwake(const QStringList &ids, bool apply)
{
    for (const QString &id : ids) {
        if (apply) {
            DeviceRegistry::instance().update(id, [](DeviceRecord &r) { r.keepAwake = 1; });
            KeepAwakeManager::instance().applyPolicy(id);
        } else {
            KeepAwakeManager::instance().restoreDefaults(id);
        }
    }
    m_statusLabel->setText(apply ? tr("Keep-awake policy applied to %n device(s).", nullptr, static_cast<int>(ids.size()))
                                 : tr("Default screen timeout restored on %n device(s).", nullptr, static_cast<int>(ids.size())));
}

void DevicesPage::actWake(const QStringList &ids)
{
    for (const QString &id : ids) {
        KeepAwakeManager::instance().wakeDevice(id);
    }
}

void DevicesPage::actRename(const QString &id)
{
    const DeviceRecord r = DeviceRegistry::instance().get(id);
    bool ok = false;
    const QString name = QInputDialog::getText(this, tr("Rename device"), tr("Friendly name for %1:").arg(id), QLineEdit::Normal, r.friendlyName, &ok);
    if (ok) {
        DeviceRegistry::instance().update(id, [&name](DeviceRecord &d) { d.friendlyName = name.trimmed(); });
    }
}

void DevicesPage::actSetNumber(const QStringList &ids)
{
    bool ok = false;
    const int start = QInputDialog::getInt(this, tr("Set number"), ids.size() > 1 ? tr("Renumber %1 devices starting at:").arg(ids.size()) : tr("Number:"),
                                           DeviceRegistry::instance().get(ids.first()).number > 0 ? DeviceRegistry::instance().get(ids.first()).number : DeviceRegistry::instance().nextFreeNumber(), 1, 9999, 1, &ok);
    if (!ok) {
        return;
    }
    DeviceRegistry::instance().renumber(DeviceRegistry::instance().sorted(DeviceRegistry::SortKey::Ip, true, ids), start);
    m_orderTimer.start();
}

void DevicesPage::actFavorite(const QStringList &ids, bool on)
{
    for (const QString &id : ids) {
        DeviceRegistry::instance().update(id, [on](DeviceRecord &r) { r.favorite = on; });
    }
}

void DevicesPage::actDisconnect(const QStringList &ids)
{
    if (!confirmBulk(tr("Disconnect (stop mirror and adb disconnect)?"), static_cast<int>(ids.size()), true)) {
        return;
    }
    for (const QString &id : ids) {
        DeviceService::instance().disconnectDevice(id);
    }
}

void DevicesPage::actForget(const QStringList &ids)
{
    if (!confirmBulk(tr("Forget these devices (remove metadata, group and number)?"), static_cast<int>(ids.size()), true)) {
        return;
    }
    for (const QString &id : ids) {
        DeviceService::instance().disconnectDevice(id);
        DeviceRegistry::instance().remove(id);
    }
}

void DevicesPage::actNormalizeReset(const QStringList &ids)
{
    QVariantMap profile;
    profile[QStringLiteral("wmSize")] = QStringLiteral("reset");
    profile[QStringLiteral("wmDensity")] = QStringLiteral("reset");
    BatchJobDialog::show(DeviceCommands::applySettings(ids, profile), this);
}

// ---------------------------------------------------------------- profiles / wifi

void DevicesPage::applyPreset(const QString &name)
{
    if (name == QLatin1String("custom")) {
        return;
    }
    const MirrorProfile p = DeviceService::presetProfile(name);
    m_sizeSlider->setValue(p.maxSize);
    m_fpsSlider->setValue(p.maxFps);
    m_bitrateSlider->setValue(std::max(1, p.bitRate / 1000000));
    DeviceService::instance().setGlobalProfile(p, false);
    m_statusLabel->setText(tr("Preset '%1' set — applies to new mirrors (use 'Apply to running' to restart).").arg(name));
}

void DevicesPage::applyCustomProfile()
{
    MirrorProfile p;
    p.name = QStringLiteral("custom");
    p.maxSize = m_sizeSlider->value();
    p.maxFps = m_fpsSlider->value();
    p.bitRate = m_bitrateSlider->value() * 1000000;
    DeviceService::instance().setGlobalProfile(p, false);
}

void DevicesPage::connectWifiRange()
{
    const QString text = m_wifiRange->text().trimmed();
    const quint16 port = static_cast<quint16>(std::clamp(m_wifiPort->text().toInt(), 1, 65535));
    ipv4::Range range;
    if (!ipv4::parseRange(text, range)) {
        m_statusLabel->setText(tr("Invalid IP, range or CIDR: %1").arg(text));
        return;
    }
    if (range.count() == 1) {
        DeviceDiscoveryService::instance().connectEndpoint(QStringLiteral("%1:%2").arg(ipv4::toString(range.first)).arg(port));
    } else {
        DeviceDiscoveryService::instance().connectRange(text, port);
    }
}

void DevicesPage::enableWifiOnSelected()
{
    QStringList usb;
    for (const QString &id : selectedIds()) {
        if (!ipv4::isTcpEndpoint(id)) {
            usb << id;
        }
    }
    if (usb.isEmpty()) {
        m_statusLabel->setText(tr("Select at least one USB device."));
        return;
    }
    for (const QString &id : usb) {
        // 1. adb tcpip 5555   2. read wlan0 IP   3. adb connect ip:5555
        AdbCommand tcpip;
        tcpip.serial = id;
        tcpip.args << QStringLiteral("tcpip") << QString::number(FarmSettings::instance().adbPort());
        tcpip.timeoutMs = 10000;
        AdbExecutor::instance().run(tcpip, this, [this, id](const AdbResult &r) {
            if (!r.ok) {
                m_statusLabel->setText(tr("tcpip failed on %1: %2").arg(id, r.error));
                return;
            }
            QTimer::singleShot(1500, this, [this, id]() {
                AdbExecutor::instance().shell(id, QStringLiteral("ip -o -4 addr show wlan0 2>/dev/null || ifconfig wlan0 2>/dev/null || ip -o -4 addr"), this, [this, id](const AdbResult &r2) {
                    const QString ip = adb::parseWlanIp(r2.stdOut);
                    if (ip.isEmpty()) {
                        m_statusLabel->setText(tr("Could not read the WiFi IP of %1 — connect manually.").arg(id));
                        return;
                    }
                    const QString endpoint = QStringLiteral("%1:%2").arg(ip).arg(FarmSettings::instance().adbPort());
                    m_statusLabel->setText(tr("%1 → %2").arg(id, endpoint));
                    DeviceDiscoveryService::instance().connectEndpoint(endpoint);
                    // Link the two identities so metadata carries over.
                    const DeviceRecord usbRec = DeviceRegistry::instance().get(id);
                    DeviceRegistry::instance().update(endpoint, [&usbRec](DeviceRecord &d) {
                        if (d.friendlyName.isEmpty()) {
                            d.friendlyName = usbRec.friendlyName;
                        }
                        if (d.group.isEmpty()) {
                            d.group = usbRec.group;
                        }
                        if (!usbRec.hwSerial.isEmpty()) {
                            d.hwSerial = usbRec.hwSerial;
                        }
                    });
                }, 8000);
            });
        });
    }
}

// ---------------------------------------------------------------- helper APK / wallpapers (ported from v2.0)

void DevicesPage::ensureHelperApk(const QString &id)
{
    if (m_helperChecked.contains(id)) {
        return;
    }
    m_helperChecked.insert(id);
    const QString apkPath = QCoreApplication::applicationDirPath() + QStringLiteral("/333FarmerWallpaperHelper.apk");
    if (!QFileInfo::exists(apkPath)) {
        return;    // optional companion; nothing to install
    }
    AdbExecutor::instance().shell(id, QStringLiteral("pm list packages %1").arg(QLatin1String(kHelperPackage)), this, [id, apkPath](const AdbResult &r) {
        if (!r.ok || r.stdOut.contains(QLatin1String(kHelperPackage))) {
            return;
        }
        AdbCommand install;
        install.serial = id;
        install.args << QStringLiteral("install") << QStringLiteral("-r") << QDir::toNativeSeparators(apkPath);
        install.timeoutMs = 120000;
        AdbExecutor::instance().run(install, nullptr, [id](const AdbResult &res) {
            ActivityLog::instance().post(res.ok ? ActivityEntry::Info : ActivityEntry::Warning, ActivityEntry::App,
                                         res.ok ? tr("Helper app installed on %1").arg(id) : tr("Helper app install failed on %1: %2").arg(id, res.error), id);
        });
    }, 10000);
}

void DevicesPage::setNumberedWallpapers(const QStringList &ids)
{
    const QString tempDir = FarmSettings::instance().dataDirectory() + QStringLiteral("/wallpapers");
    QDir().mkpath(tempDir);
    const QString svgPath = QCoreApplication::applicationDirPath() + QStringLiteral("/wallpaper_template.svg");
    auto *job = new BatchJob(tr("Numbered wallpaper"), ids, [tempDir, svgPath](const QString &id, CancellationToken token, BatchJob::DoneFn done) {
        const DeviceRecord rec = DeviceRegistry::instance().get(id);
        const int number = rec.number;
        QImage wallpaper(1080, 1920, QImage::Format_ARGB32);
        wallpaper.fill(Qt::black);
        if (QFileInfo::exists(svgPath)) {
            auto document = lunasvg::Document::loadFromFile(svgPath.toStdString());
            if (document) {
                auto bitmap = document->renderToBitmap(1080, 1920);
                if (bitmap.valid()) {
                    QImage svg(bitmap.data(), static_cast<int>(bitmap.width()), static_cast<int>(bitmap.height()), static_cast<int>(bitmap.stride()), QImage::Format_RGBA8888);
                    QPainter p(&wallpaper);
                    p.drawImage(0, 0, svg);
                }
            }
        }
        QPainter painter(&wallpaper);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(Qt::white);
        painter.setFont(QFont(QStringLiteral("Arial"), 200, QFont::Black));
        painter.drawText(QRect(0, 350, 1080, 200), Qt::AlignCenter, QString::number(number));
        painter.end();
        const QString local = QStringLiteral("%1/wallpaper_%2.png").arg(tempDir).arg(number);
        wallpaper.save(local, "PNG");
        const QString remote = QStringLiteral("/sdcard/Android/data/%1/files/wallpaper.png").arg(QLatin1String(kHelperPackage));
        AdbExecutor::instance().shell(id, QStringLiteral("mkdir -p /sdcard/Android/data/%1/files").arg(QLatin1String(kHelperPackage)), nullptr, [id, local, remote, done, token](const AdbResult &) {
            AdbCommand push;
            push.serial = id;
            push.args << QStringLiteral("push") << QDir::toNativeSeparators(local) << remote;
            push.timeoutMs = 60000;
            AdbExecutor::instance().run(push, nullptr, [id, remote, done](const AdbResult &p) {
                if (!p.ok) {
                    done(false, p.error);
                    return;
                }
                AdbExecutor::instance().shell(id, QStringLiteral("am start -n %1/.SetWallpaperActivity -e file %2").arg(QLatin1String(kHelperPackage), remote), nullptr, [done](const AdbResult &s) {
                    done(s.ok && !s.combined().contains(QLatin1String("Error")), s.ok ? QStringLiteral("wallpaper set") : s.error);
                }, 15000);
            }, token);
        }, 8000);
    }, 4);
    JobManager::instance().startJob(job);
    BatchJobDialog::show(job, this);
}

} // namespace farm
