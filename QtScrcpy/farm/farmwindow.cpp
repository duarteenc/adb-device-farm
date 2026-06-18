#include "farmwindow.h"
#include "adbcontrollerdialog.h"
#include "cursorbadge.h"
#include "devicesdialog.h"
#include "installapkdialog.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <memory>
#include <utility>

#include <QCheckBox>
#include <QCoreApplication>
#include <QDir>
#include <QEvent>
#include <QFileInfo>
#include <QFont>
#include <QFrame>
#include <QGradient>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QImage>
#include <QInputDialog>
#include <QIntValidator>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QLinearGradient>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QProcess>
#include <QPushButton>
#include <QRandomGenerator>
#include <QResizeEvent>
#include <QRubberBand>
#include <QScrollArea>
#include <QSettings>
#include <QSlider>
#include <QCursor>
#include <QTimer>
#include <QVBoxLayout>
#include <QWheelEvent>

#include <lunasvg.h>

#include "config.h"
#include "devicetile.h"
#include "focuspanel.h"

namespace {
// Resolve a usable adb binary. Config's path is empty by default; the core
// instead bundles adb.exe next to the executable, so fall back to that.
QString resolveAdbPath()
{
    QString path = Config::getInstance().getAdbPath();
    if (path.isEmpty()) {
        path = QCoreApplication::applicationDirPath() + "/adb.exe";
    }
    return path;
}

constexpr int kGridSpacing = 10;
constexpr int kGridMargin = 12;
constexpr int kMinTileWidth = 160;    // floor: phones unreadable below this
constexpr int kMaxConcurrent = 4;     // simultaneous connection setups (smooths the
                                      // server-start + first-frame decode spike)
constexpr quint16 kBasePort = 27183;  // reverse-tunnel port base (unique per device)
// Every device is forced to this resolution/density on connect so all phones
// mirror and (most importantly) accept control coordinates identically.
constexpr const char *kNormalizedSize = "1080x2220";
constexpr const char *kNormalizedDensity = "480";

const char *kStyle = R"(
FarmWindow { background:#0b0f17; }
QWidget { background:transparent; color:#e2e8f0; font-size:12px; }
#controlPanel { background:#121826; border-right:1px solid #1e2636; }
QLabel { background:transparent; }
#panelTitle { font-size:16px; font-weight:bold; padding:2px 0 8px 0; }
QPushButton { background:transparent; border:1px solid #2a344a; border-radius:5px; padding:6px 10px; }
QPushButton:hover { background:#26314a; }
QPushButton#primary { background:#2563eb; border:none; }
QPushButton#primary:hover { background:#3b82f6; }
QLineEdit { background:#0b0f17; border:1px solid #2a344a; border-radius:4px; padding:4px; }
QCheckBox { background:transparent; padding:2px; }
QLabel#hint { color:#7c8aa0; }
QScrollArea, QScrollArea > QWidget { background:transparent; border:none; }
QScrollArea::corner { background:#121826; }
#gridHost { background:#0b0f17; }
#selectorScroll { background:#121826; }
#selectorScroll::corner { background:#121826; }
#selectorGridHost { background:#121826; }
QScrollBar:vertical { background:#121826; width:8px; border:none; margin:0px; }
QScrollBar::handle:vertical { background:#2a344a; border-radius:4px; min-height:20px; margin:0px; }
QScrollBar::handle:vertical:hover { background:#3b4a5a; }
QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { border:none; background:none; height:0px; }
QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { background:none; }
QScrollBar:horizontal { background:#121826; height:0px; }
QSlider::groove:horizontal { height:4px; background:#2a344a; border-radius:2px; }
QSlider::handle:horizontal { width:14px; margin:-6px 0; border-radius:7px; background:#3b82f6; }
QSlider::sub-page:horizontal { background:#2563eb; border-radius:2px; }
#tileOverlay { background: transparent; }
#tileNum { background: transparent; color:#ffffff; font-size:16px; font-weight:bold; }
#tileModel { background: transparent; color:#f1f5f9; font-size:11px; font-weight:bold; }
#tileIp { background: transparent; color:#ffffff; font-size:10px; font-weight:bold; }
#tileNum[sel="true"], #tileModel[sel="true"], #tileIp[sel="true"] { color:#3b9dff; }
#tileFps { background: transparent; color:#dbe4f0; font-size:9px; }
)";
}

FarmWindow::FarmWindow(QWidget *parent)
    : QWidget(parent)
{
    setWindowTitle(tr("333Farmer - v1.0"));
    setStyleSheet(QString::fromUtf8(kStyle));
    loadGroups();

    // --- Scrollable grid (right) ---
    m_gridHost = new QWidget;
    m_gridHost->setObjectName("gridHost");
    m_grid = new QGridLayout(m_gridHost);
    m_grid->setContentsMargins(kGridMargin, kGridMargin, kGridMargin, kGridMargin);
    m_grid->setSpacing(kGridSpacing);

    // Marquee selection: tiles are mouse-transparent (see DeviceTile), so the
    // grid background receives press/drag to draw a selection rubber band.
    m_gridHost->installEventFilter(this);
    m_rubberBand = new QRubberBand(QRubberBand::Rectangle, m_gridHost);

    m_scroll = new QScrollArea(this);
    m_scroll->setWidgetResizable(true);
    m_scroll->setWidget(m_gridHost);
    m_scroll->viewport()->installEventFilter(this);    // relayout when grid width changes

    m_cursorBadge = new CursorBadge();    // top-level float; follows the cursor while there's a selection
    m_cursorBadge->hide();
    m_badgeTimer = new QTimer(this);
    m_badgeTimer->setInterval(30);
    connect(m_badgeTimer, &QTimer::timeout, this, &FarmWindow::tickCursorBadge);
    m_badgeTimer->start();

    m_focusPanel = new FocusPanel(this);
    m_focusPanel->hide();
    connect(m_focusPanel, &FocusPanel::closed, this, [this](const QString &s) {
        if (m_tiles.contains(s)) {
            m_tiles[s]->setUnderControl(false);
        }
        m_focusSerial.clear();
        m_focusPanel->hide();
        relayout();
    });
    connect(m_focusPanel, &FocusPanel::adbControllerRequested, this, [this](const QString &serial) {
        // Target the host first, then any other selected devices (don't clobber
        // the user's selection — match the host's broadcast set).
        QStringList targets;
        targets << serial;
        for (const QString &s : m_order) {
            if (s != serial && m_selectedSerials.contains(s)) {
                targets << s;
            }
        }
        openAdbController(targets);
    });
    connect(m_focusPanel, &FocusPanel::installApkRequested, this,
            &FarmWindow::openInstallApk);

    auto *root = new QHBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);
    root->addWidget(buildControlPanel(), 0);
    root->addWidget(m_focusPanel, 0);
    root->addWidget(m_scroll, 1);

    connect(&qsc::IDeviceManage::getInstance(), &qsc::IDeviceManage::deviceConnected, this,
            &FarmWindow::onDeviceConnected);
    connect(&qsc::IDeviceManage::getInstance(), &qsc::IDeviceManage::deviceDisconnected, this,
            &FarmWindow::onDeviceDisconnected);
    connect(&m_adb, &qsc::AdbProcess::adbProcessResult, this, &FarmWindow::onAdbResult);

    // Set minimum window size
    setMinimumSize(1280, 720);

    refreshDevices();
}

FarmWindow::~FarmWindow()
{
    qsc::IDeviceManage::getInstance().disconnectAllDevice();
}

QWidget *FarmWindow::buildControlPanel()
{
    auto *panel = new QWidget(this);
    panel->setObjectName("controlPanel");
    panel->setFixedWidth(280);

    // Title with icon
    auto *titleContainer = new QWidget(panel);
    auto *titleLayout = new QHBoxLayout(titleContainer);
    titleLayout->setContentsMargins(0, 0, 0, 0);
    titleLayout->setSpacing(8);

    auto *iconLabel = new QLabel(titleContainer);
    QPixmap iconPixmap(":/res/QtScrcpy.ico");
    iconLabel->setPixmap(iconPixmap.scaled(32, 32, Qt::KeepAspectRatio, Qt::SmoothTransformation));

    auto *title = new QLabel(tr("333Farmer"), titleContainer);
    title->setObjectName("panelTitle");

    titleLayout->addWidget(iconLabel);
    titleLayout->addWidget(title);
    titleLayout->addStretch();

    auto *refreshBtn = new QPushButton(tr("Refresh"), panel);
    auto *devicesBtn = new QPushButton(tr("⁙ Dispositivos"), panel);
    auto *mirrorAllBtn = new QPushButton(tr("Mirror All"), panel);
    mirrorAllBtn->setObjectName("primary");
    auto *stopAllBtn = new QPushButton(tr("Stop All"), panel);

    // Sliders. A small helper builds a labelled slider row.
    auto makeSlider = [panel](const QString &name, int lo, int hi, int value, QLabel **valueOut) {
        auto *box = new QVBoxLayout();
        box->setSpacing(2);
        auto *row = new QHBoxLayout();
        auto *label = new QLabel(name, panel);
        auto *valueLabel = new QLabel(QString::number(value), panel);
        valueLabel->setObjectName("hint");
        valueLabel->setAlignment(Qt::AlignRight);
        row->addWidget(label);
        row->addWidget(valueLabel);
        auto *slider = new QSlider(Qt::Horizontal, panel);
        slider->setRange(lo, hi);
        slider->setValue(value);
        box->addLayout(row);
        box->addWidget(slider);
        *valueOut = valueLabel;
        return std::make_pair(box, slider);
    };

    auto tileRow = makeSlider(tr("Tile size"), kMinTileWidth, 360, m_tileWidth, &m_tileSizeValue);
    auto hostRow = makeSlider(tr("Host screen"), 480, 1240, m_hostHeight, &m_hostSizeValue);
    auto qualityRow = makeSlider(tr("Quality (px)"), 320, 1280, static_cast<int>(m_maxSize), &m_qualityValue);
    auto fpsRow = makeSlider(tr("Frame rate"), 15, 60, static_cast<int>(m_maxFps), &m_fpsValue);

    connect(tileRow.second, &QSlider::valueChanged, this, &FarmWindow::setTileSize);
    connect(hostRow.second, &QSlider::valueChanged, this, &FarmWindow::setHostSize);
    connect(qualityRow.second, &QSlider::valueChanged, this, &FarmWindow::setQuality);
    connect(fpsRow.second, &QSlider::valueChanged, this, &FarmWindow::setFrameRate);

    auto *groupCheck = new QCheckBox(tr("Control All (group input)"), panel);

    auto *smallCtrlCheck = new QCheckBox(tr("Control in small view"), panel);
    smallCtrlCheck->setChecked(m_smallViewControl);
    smallCtrlCheck->setToolTip(tr("On: tap/drag a grid tile controls that phone.\n"
                                  "Off: the grid is for marquee selection only."));

    auto *keepScreenOnBtn = new QPushButton(tr("Keep Screen On (selected)"), panel);
    keepScreenOnBtn->setToolTip(tr("Disable auto screen timeout on selected devices"));

    auto *restoreScreenTimeoutBtn = new QPushButton(tr("Restore Screen Timeout (selected)"), panel);
    restoreScreenTimeoutBtn->setToolTip(tr("Restore default screen timeout (30 seconds)"));

    // ---- WiFi connect (GenFarmer-style: a port box + start/end IP octet rows) ----
    // Octet field factory: a small centred 0-255 box. Disabled boxes (the end
    // row's first three) just mirror the start subnet and aren't editable.
    auto makeOctet = [panel](const QString &initial, bool enabled) {
        auto *e = new QLineEdit(initial, panel);
        e->setEnabled(enabled);
        e->setAlignment(Qt::AlignCenter);
        e->setMaxLength(3);
        e->setFixedWidth(46);
        e->setValidator(new QIntValidator(0, 255, e));
        e->setStyleSheet(QStringLiteral(
            "QLineEdit{background:#0f1422;border:1px solid #2a344a;border-radius:6px;"
            "padding:7px 0;color:#e2e8f0;font-weight:bold;}"
            "QLineEdit:focus{border:1px solid #3b9dff;}"
            "QLineEdit:disabled{background:#0c111c;color:#5b6680;}"));
        return e;
    };
    auto dashLabel = [panel]() {
        auto *d = new QLabel(QStringLiteral("-"), panel);
        d->setStyleSheet(QStringLiteral("color:#5b6680;font-weight:bold;"));
        return d;
    };

    // Port row.
    m_portEdit = new QLineEdit(QStringLiteral("5555"), panel);
    m_portEdit->setFixedWidth(72);
    m_portEdit->setValidator(new QIntValidator(1, 65535, m_portEdit));
    m_portEdit->setStyleSheet(QStringLiteral(
        "QLineEdit{background:#0f1422;border:1px solid #2a344a;border-radius:6px;"
        "padding:7px 8px;color:#e2e8f0;}"
        "QLineEdit:focus{border:1px solid #3b9dff;}"));
    auto *portRow = new QWidget(panel);
    auto *portLay = new QHBoxLayout(portRow);
    portLay->setContentsMargins(0, 0, 0, 0);
    portLay->setSpacing(8);
    portLay->addWidget(new QLabel(tr("Puerto"), panel));
    portLay->addWidget(m_portEdit);
    portLay->addStretch(1);

    // Start IP row (all four octets editable).
    m_octA = makeOctet(QStringLiteral("192"), true);
    m_octB = makeOctet(QStringLiteral("168"), true);
    m_octC = makeOctet(QStringLiteral("1"), true);
    m_octStart = makeOctet(QStringLiteral("1"), true);
    auto *startRow = new QWidget(panel);
    auto *startLay = new QHBoxLayout(startRow);
    startLay->setContentsMargins(0, 0, 0, 0);
    startLay->setSpacing(4);
    for (QLineEdit *o : {m_octA, m_octB, m_octC, m_octStart}) {
        if (o != m_octA) {
            startLay->addWidget(dashLabel());
        }
        startLay->addWidget(o);
    }
    startLay->addStretch(1);

    // End IP row: first three octets locked to the start's subnet, only the last
    // octet is editable (the range's end address).
    m_octEndA = makeOctet(QStringLiteral("192"), false);
    m_octEndB = makeOctet(QStringLiteral("168"), false);
    m_octEndC = makeOctet(QStringLiteral("1"), false);
    m_octEnd = makeOctet(QStringLiteral("255"), true);
    auto *endRow = new QWidget(panel);
    auto *endLay = new QHBoxLayout(endRow);
    endLay->setContentsMargins(0, 0, 0, 0);
    endLay->setSpacing(4);
    for (QLineEdit *o : {m_octEndA, m_octEndB, m_octEndC, m_octEnd}) {
        if (o != m_octEndA) {
            endLay->addWidget(dashLabel());
        }
        endLay->addWidget(o);
    }
    endLay->addStretch(1);

    // Keep the (disabled) end subnet mirroring the start as the user edits it.
    connect(m_octA, &QLineEdit::textChanged, m_octEndA, &QLineEdit::setText);
    connect(m_octB, &QLineEdit::textChanged, m_octEndB, &QLineEdit::setText);
    connect(m_octC, &QLineEdit::textChanged, m_octEndC, &QLineEdit::setText);

    auto *connectBtn = new QPushButton(tr("Connect"), panel);
    auto *wifiHint = new QLabel(
        tr("IP inicial → IP final (mismo /24). Conecta todo el rango."), panel);
    wifiHint->setObjectName("hint");
    wifiHint->setWordWrap(true);
    auto *enableWifiBtn = new QPushButton(tr("Enable WiFi (selected)"), panel);

    m_statusBar = new QLabel(tr("Ready."), panel);
    m_statusBar->setObjectName("hint");
    m_statusBar->setWordWrap(true);

    auto sep = [panel]() {
        auto *line = new QFrame(panel);
        line->setFrameShape(QFrame::HLine);
        line->setStyleSheet("color:#1e2636;");
        return line;
    };

    auto *col = new QVBoxLayout(panel);
    col->setContentsMargins(12, 12, 12, 12);
    col->setSpacing(8);
    col->addWidget(titleContainer);
    col->addWidget(refreshBtn);
    col->addWidget(devicesBtn);
    col->addWidget(mirrorAllBtn);
    col->addWidget(stopAllBtn);
    col->addWidget(sep());
    col->addLayout(tileRow.first);
    col->addLayout(hostRow.first);
    col->addLayout(qualityRow.first);
    col->addLayout(fpsRow.first);
    col->addWidget(new QLabel(tr("Quality/FPS apply on next Mirror All."), panel));
    col->itemAt(col->count() - 1)->widget()->setObjectName("hint");
    col->addWidget(sep());
    col->addWidget(smallCtrlCheck);
    col->addWidget(groupCheck);
    col->addWidget(keepScreenOnBtn);
    col->addWidget(restoreScreenTimeoutBtn);
    col->addWidget(sep());
    col->addWidget(buildSelectorSection());
    col->addWidget(sep());
    col->addWidget(new QLabel(tr("WiFi connect"), panel));
    col->addWidget(portRow);
    col->addWidget(startRow);
    col->addWidget(endRow);
    col->addWidget(wifiHint);
    col->addWidget(connectBtn);
    col->addWidget(enableWifiBtn);
    col->addStretch(1);
    col->addWidget(m_statusBar);

    connect(refreshBtn, &QPushButton::clicked, this, &FarmWindow::refreshDevices);
    connect(devicesBtn, &QPushButton::clicked, this, &FarmWindow::openDevicesDialog);
    connect(mirrorAllBtn, &QPushButton::clicked, this, &FarmWindow::mirrorAll);
    connect(stopAllBtn, &QPushButton::clicked, this, &FarmWindow::stopAll);
    connect(connectBtn, &QPushButton::clicked, this, &FarmWindow::connectWifi);
    connect(m_octStart, &QLineEdit::returnPressed, this, &FarmWindow::connectWifi);
    connect(m_octEnd, &QLineEdit::returnPressed, this, &FarmWindow::connectWifi);
    connect(enableWifiBtn, &QPushButton::clicked, this, &FarmWindow::enableWifiSelected);
    connect(keepScreenOnBtn, &QPushButton::clicked, this, &FarmWindow::keepScreenOnSelected);
    connect(restoreScreenTimeoutBtn, &QPushButton::clicked, this, &FarmWindow::restoreScreenTimeoutSelected);
    connect(groupCheck, &QCheckBox::toggled, this, &FarmWindow::setGroupMode);
    connect(smallCtrlCheck, &QCheckBox::toggled, this, &FarmWindow::setSmallViewControl);

    // Make the (potentially tall) panel scroll instead of clipping.
    auto *panelScroll = new QScrollArea(this);
    panelScroll->setWidgetResizable(true);
    panelScroll->setFixedWidth(296);
    panelScroll->setFrameShape(QFrame::NoFrame);
    panelScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    panelScroll->setWidget(panel);
    return panelScroll;
}

QWidget *FarmWindow::buildSelectorSection()
{
    auto *w = new QWidget;
    auto *v = new QVBoxLayout(w);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(6);

    auto *devLabel = new QLabel(tr("Devices"), w);
    devLabel->setStyleSheet("font-weight:bold;");
    v->addWidget(devLabel);

    auto *selAllBtn = new QPushButton(tr("Select all"), w);
    auto *clearBtn = new QPushButton(tr("Clear"), w);
    auto *selRow = new QHBoxLayout();
    selRow->addWidget(selAllBtn);
    selRow->addWidget(clearBtn);
    v->addLayout(selRow);

    auto *mirrorSelBtn = new QPushButton(tr("Mirror selected"), w);
    mirrorSelBtn->setObjectName("primary");
    v->addWidget(mirrorSelBtn);

    auto *hint = new QLabel(tr("Drag on the grid to select. Control via the host."), w);
    hint->setObjectName("hint");
    hint->setWordWrap(true);
    v->addWidget(hint);

    m_selectorGridHost = new QWidget(w);
    m_selectorGridHost->setObjectName("selectorGridHost");
    m_selectorGridHost->setMouseTracking(true);
    m_selectorGridHost->setCursor(Qt::PointingHandCursor);
    m_selectorGrid = new QGridLayout(m_selectorGridHost);
    m_selectorGrid->setContentsMargins(0, 0, 0, 0);
    m_selectorGrid->setSpacing(4);
    m_selectorGrid->setAlignment(Qt::AlignTop | Qt::AlignLeft);

    // Create rubber band for visual drag selection
    m_selectorRubberBand = new QRubberBand(QRubberBand::Rectangle, m_selectorGridHost);
    m_selectorRubberBand->hide();

    auto *gridScroll = new QScrollArea(w);
    gridScroll->setObjectName("selectorScroll");
    gridScroll->setWidgetResizable(true);
    gridScroll->setFixedHeight(150);
    gridScroll->setFrameShape(QFrame::NoFrame);
    gridScroll->setWidget(m_selectorGridHost);
    m_selectorGridHost->installEventFilter(this);
    v->addWidget(gridScroll);

    auto *grpLabel = new QLabel(tr("Groups"), w);
    grpLabel->setStyleSheet("font-weight:bold;");
    auto *addGrpBtn = new QPushButton(tr("+"), w);
    addGrpBtn->setFixedWidth(28);
    auto *grpRow = new QHBoxLayout();
    grpRow->addWidget(grpLabel, 1);
    grpRow->addWidget(addGrpBtn, 0);
    v->addLayout(grpRow);

    auto *groupsHost = new QWidget(w);
    m_groupsLayout = new QVBoxLayout(groupsHost);
    m_groupsLayout->setContentsMargins(0, 0, 0, 0);
    m_groupsLayout->setSpacing(4);
    v->addWidget(groupsHost);

    connect(selAllBtn, &QPushButton::clicked, this, &FarmWindow::selectAllDevices);
    connect(clearBtn, &QPushButton::clicked, this, &FarmWindow::clearDeviceSelection);
    connect(mirrorSelBtn, &QPushButton::clicked, this, &FarmWindow::mirrorSelected);
    connect(addGrpBtn, &QPushButton::clicked, this, &FarmWindow::createGroup);

    rebuildGroups();
    return w;
}

void FarmWindow::rebuildNumbering()
{
    auto lastOctet = [](const QString &s) {
        return s.section(':', 0, 0).section('.', -1).toInt();
    };
    QStringList sorted = m_available;
    std::sort(sorted.begin(), sorted.end(),
              [&](const QString &a, const QString &b) { return lastOctet(a) < lastOctet(b); });
    m_numbering.clear();
    int n = 1;
    for (const QString &s : sorted) {
        m_numbering.insert(s, n++);
    }

    // Create placeholder tiles for ALL available devices (GenFarmer style)
    // This reserves their positions even before they connect
    for (const QString &serial : sorted) {
        if (!m_tiles.contains(serial)) {
            // Parent to the grid host up front: with the relayout deferred
            // (scheduleRelayout), an unparented tile would briefly be a top-level
            // window — a white, un-themed phone floating on screen.
            auto *tile = new DeviceTile(serial, m_gridHost);
            tile->setTileWidth(m_tileWidth);
            tile->setNumber(m_numbering.value(serial, 0));
            tile->setControllable(m_smallViewControl);
            // Set placeholder state - show IP but waiting for connection
            tile->setModel("...");  // Placeholder text
            tile->setStatusText("waiting");

            // Connect signals (needed when placeholder becomes active)
            connect(tile, &DeviceTile::mouseInput, this, &FarmWindow::onTileMouse);
            connect(tile, &DeviceTile::wheelInput, this, &FarmWindow::onTileWheel);
            connect(tile, &DeviceTile::keyInput, this, &FarmWindow::onTileKey);
            connect(tile, &DeviceTile::doubleClicked, this, &FarmWindow::onTileDoubleClicked);
            connect(tile, &DeviceTile::reloadRequested, this, &FarmWindow::onTileReloadRequested);
            connect(tile, &DeviceTile::contextMenuRequested, this, &FarmWindow::onTileContextMenuRequested);

            m_tiles.insert(serial, tile);
        }
    }

    // Rebuild m_order from scratch using ALL sorted devices
    m_order = sorted;
    scheduleRelayout();
}

void FarmWindow::rebuildSelector()
{
    if (!m_selectorGrid) {
        return;
    }
    while (QLayoutItem *item = m_selectorGrid->takeAt(0)) {
        if (item->widget()) {
            item->widget()->deleteLater();
        }
        delete item;
    }
    m_selectorButtons.clear();

    QList<QString> serials = m_numbering.keys();
    std::sort(serials.begin(), serials.end(),
              [this](const QString &a, const QString &b) { return m_numbering[a] < m_numbering[b]; });

    const int cols = 5;
    int i = 0;
    for (const QString &s : serials) {
        auto *b = new QPushButton(QString::number(m_numbering[s]));
        b->setCheckable(true);
        b->setFixedSize(40, 28);
        b->setToolTip(s.section(':', 0, 0));

        // Install event filter to detect double clicks
        b->installEventFilter(this);
        b->setProperty("deviceSerial", s);

        connect(b, &QPushButton::clicked, this, [this, s] { toggleSelection(s); });
        m_selectorGrid->addWidget(b, i / cols, i % cols);
        m_selectorButtons.insert(s, b);
        ++i;
    }
    updateSelectorStyles();
}

void FarmWindow::updateSelectorStyles()
{
    for (auto it = m_selectorButtons.begin(); it != m_selectorButtons.end(); ++it) {
        updateSelectorButtonStyle(it.key());
    }
}

void FarmWindow::updateSelectorButtonStyle(const QString &serial, bool preview)
{
    QPushButton *b = m_selectorButtons.value(serial, nullptr);
    if (!b) {
        return;
    }

    const bool selected = m_selectedSerials.contains(serial);
    const bool mirroring = m_tiles.contains(serial);

    QString bg, border;
    int borderWidth = 1;

    if (preview) {
        // Preview style: light blue with thicker border
        bg = QStringLiteral("#3b82f6");
        border = QStringLiteral("#60a5fa");
        borderWidth = 2;
    } else if (selected) {
        bg = QStringLiteral("#2563eb");
        border = mirroring ? QStringLiteral("#22c55e") : QStringLiteral("#2a344a");
    } else {
        bg = QStringLiteral("#1c2436");
        border = mirroring ? QStringLiteral("#22c55e") : QStringLiteral("#2a344a");
    }

    b->setChecked(selected || preview);
    b->setStyleSheet(QStringLiteral("QPushButton{background:%1;border:%2px solid %3;"
                                    "border-radius:4px;color:#e2e8f0;font-size:11px;}")
                         .arg(bg)
                         .arg(borderWidth)
                         .arg(border));
}

void FarmWindow::toggleSelection(const QString &serial)
{
    if (m_selectedSerials.contains(serial)) {
        m_selectedSerials.remove(serial);
    } else {
        m_selectedSerials.insert(serial);
    }
    updateSelectorStyles();
    updateTileSelectionStyles();
}

void FarmWindow::updateTileSelectionStyles()
{
    for (auto it = m_tiles.begin(); it != m_tiles.end(); ++it) {
        it.value()->setSelected(m_selectedSerials.contains(it.key()));
    }
    updateGroupStyles();    // group cards mirror the selection (chips, counts, "select all")
    updateHostTargets();    // keep the host's broadcast set in sync with the selection
}

QString FarmWindow::tileAt(const QPoint &point) const
{
    for (auto it = m_tiles.begin(); it != m_tiles.end(); ++it) {
        // Skip tiles hidden by the group "isolate" view — their stale geometry
        // must not capture clicks/drags.
        if (it.value()->isVisible() && it.value()->geometry().contains(point)) {
            return it.key();
        }
    }
    return QString();
}

QString FarmWindow::selectorButtonAt(const QPoint &point) const
{
    for (auto it = m_selectorButtons.begin(); it != m_selectorButtons.end(); ++it) {
        QPushButton *btn = it.value();
        if (btn->geometry().contains(point)) {
            return it.key();
        }
    }
    return QString();
}

void FarmWindow::tickCursorBadge()
{
    // The chip follows the cursor while any device is selected. During a marquee
    // drag it previews how many tiles the rubber band currently covers.
    int count = m_dragging ? static_cast<int>(m_tilesUnderRubber.size())
                           : static_cast<int>(m_selectedSerials.size());
    if (count <= 0) {
        if (m_cursorBadge->isVisible()) {
            m_cursorBadge->hide();
        }
        return;
    }
    m_cursorBadge->setCount(count);
    m_cursorBadge->moveToCursor(QCursor::pos());
    if (!m_cursorBadge->isVisible()) {
        m_cursorBadge->show();
        m_cursorBadge->raise();
    }
}

void FarmWindow::updateHostTargets()
{
    if (!m_focusPanel || m_focusSerial.isEmpty()) {
        return;
    }
    QList<QString> targets;
    if (m_groupMode) {
        targets = m_order;    // "Control All": every connected device
    } else {
        targets.append(m_focusSerial);    // host first
        for (const QString &s : m_order) {
            if (s != m_focusSerial && m_selectedSerials.contains(s)) {
                targets.append(s);
            }
        }
    }
    m_focusPanel->setTargets(targets);
}

void FarmWindow::applyRubberSelection(const QRect &rect, bool additive)
{
    if (!additive) {
        m_selectedSerials.clear();
    }
    for (auto it = m_tiles.begin(); it != m_tiles.end(); ++it) {
        if (it.value()->isVisible() && it.value()->geometry().intersects(rect)) {
            m_selectedSerials.insert(it.key());
        }
    }
    updateSelectorStyles();
    updateTileSelectionStyles();
    m_statusBar->setText(tr("%1 selected").arg(static_cast<int>(m_selectedSerials.size())));
}

void FarmWindow::selectAllDevices()
{
    const QList<QString> serials = m_numbering.keys();
    for (const QString &s : serials) {
        m_selectedSerials.insert(s);
    }
    updateSelectorStyles();
    updateTileSelectionStyles();
}

void FarmWindow::clearDeviceSelection()
{
    m_selectedSerials.clear();
    updateSelectorStyles();
    updateTileSelectionStyles();
}

void FarmWindow::mirrorSelected()
{
    if (m_selectedSerials.isEmpty()) {
        m_statusBar->setText(tr("No devices selected."));
        return;
    }
    for (const QString &serial : m_selectedSerials) {
        bool alreadyConnected = qsc::IDeviceManage::getInstance().getDevice(serial) != nullptr;
        if (m_available.contains(serial) && !alreadyConnected
            && !m_connecting.contains(serial) && !m_pending.contains(serial)) {
            m_pending.append(serial);
        }
    }
    pumpConnectQueue();
}

void FarmWindow::createGroup()
{
    // Groups may be created empty — devices are assigned later from the tile
    // right-click menu ("Agregar al grupo").
    bool ok = false;
    const QString name =
        QInputDialog::getText(this, tr("New group"), tr("Group name:"), QLineEdit::Normal,
                              QString(), &ok)
            .trimmed();
    if (!ok || name.isEmpty()) {
        return;
    }
    // Seed with the current selection if any; otherwise an empty group.
    QStringList serials = m_groups.value(name);
    for (const QString &s : m_selectedSerials) {
        if (!serials.contains(s)) {
            serials << s;
        }
    }
    m_groups.insert(name, serials);
    saveGroups();
    rebuildGroups();
    m_statusBar->setText(tr("Group '%1' saved (%2 devices)").arg(name).arg(serials.size()));
}

void FarmWindow::applyGroup(const QString &name)
{
    if (!m_groups.contains(name)) {
        return;
    }
    m_selectedSerials.clear();
    for (const QString &s : m_groups[name]) {
        m_selectedSerials.insert(s);
    }
    updateSelectorStyles();
    updateTileSelectionStyles();
    m_statusBar->setText(tr("Group '%1' selected (%2)").arg(name).arg(m_groups[name].size()));
}

void FarmWindow::addDevicesToGroup(const QString &name, const QStringList &serials)
{
    QStringList list = m_groups.value(name);
    for (const QString &s : serials) {
        if (!list.contains(s)) {
            list << s;
        }
    }
    m_groups.insert(name, list);
    saveGroups();
    rebuildGroups();
    if (m_isolatedGroup == name) {
        relayout();    // isolated view must reflect the new members
    }
    m_statusBar->setText(
        tr("Added %1 device(s) to '%2' (%3)").arg(serials.size()).arg(name).arg(list.size()));
}

void FarmWindow::removeDevicesFromGroup(const QString &name, const QStringList &serials)
{
    if (!m_groups.contains(name)) {
        return;
    }
    QStringList list = m_groups.value(name);
    for (const QString &s : serials) {
        list.removeAll(s);
    }
    m_groups.insert(name, list);
    saveGroups();
    rebuildGroups();
    if (m_isolatedGroup == name) {
        relayout();    // isolated view must reflect the removed members
    }
    m_statusBar->setText(
        tr("Removed %1 device(s) from '%2' (%3)").arg(serials.size()).arg(name).arg(list.size()));
}

void FarmWindow::newGroupWithDevices(const QStringList &serials)
{
    bool ok = false;
    const QString name = QInputDialog::getText(this, tr("Nuevo grupo"), tr("Nombre del grupo:"),
                                               QLineEdit::Normal, QString(), &ok)
                             .trimmed();
    if (!ok || name.isEmpty()) {
        return;
    }
    addDevicesToGroup(name, serials);
}

void FarmWindow::buildAddToGroupMenu(QMenu *parent, const QStringList &serials)
{
    QMenu *sub = parent->addMenu(tr("Agregar al grupo"));

    QStringList names = m_groups.keys();
    names.sort();
    for (const QString &name : names) {
        const QStringList members = m_groups.value(name);
        // Checked when every targeted device is already in the group.
        bool allIn = !serials.isEmpty();
        for (const QString &s : serials) {
            if (!members.contains(s)) {
                allIn = false;
                break;
            }
        }
        QAction *act = sub->addAction(tr("%1 (%2)").arg(name).arg(members.size()));
        act->setCheckable(true);
        act->setChecked(allIn);
        connect(act, &QAction::triggered, this, [this, name, serials, allIn]() {
            if (allIn) {
                removeDevicesFromGroup(name, serials);
            } else {
                addDevicesToGroup(name, serials);
            }
        });
    }

    if (!names.isEmpty()) {
        sub->addSeparator();
    }
    QAction *newAct = sub->addAction(tr("Nuevo grupo…"));
    connect(newAct, &QAction::triggered, this, [this, serials]() { newGroupWithDevices(serials); });
}

QString FarmWindow::chipLabelFor(const QString &serial) const
{
    if (m_numbering.contains(serial)) {
        return QString::number(m_numbering.value(serial));
    }
    // Offline / not enumerated: fall back to the IP's last octet.
    const QString tail = serial.section(':', 0, 0).section('.', -1);
    return tail.isEmpty() ? QStringLiteral("?") : tail;
}

void FarmWindow::rebuildGroups()
{
    if (!m_groupsLayout) {
        return;
    }
    while (QLayoutItem *item = m_groupsLayout->takeAt(0)) {
        if (item->widget()) {
            item->widget()->deleteLater();
        }
        delete item;
    }
    m_groupChips.clear();
    m_groupCountLabels.clear();
    m_groupSelectAll.clear();

    QStringList names = m_groups.keys();
    names.sort();
    for (const QString &name : names) {
        const QStringList members = m_groups.value(name);
        const bool collapsed = m_collapsedGroups.contains(name);

        // ---- Card ----
        auto *card = new QWidget;
        card->setObjectName(QStringLiteral("groupCard"));
        card->setStyleSheet(QStringLiteral(
            "#groupCard{background:#141a28;border:1px solid #232c40;border-radius:6px;}"));
        auto *cardLay = new QVBoxLayout(card);
        cardLay->setContentsMargins(8, 6, 8, 8);
        cardLay->setSpacing(6);

        // ---- Header: chevron · name · (sel/total) · edit · delete · eye ----
        auto *header = new QHBoxLayout();
        header->setSpacing(6);

        // Plain BMP Unicode symbols via QChar (encoding-independent — avoids any
        // source-charset surprise) with WIDE font coverage, so whatever font Qt
        // resolves always has a glyph. CRITICAL: padding:0;margin:0 — a default
        // QPushButton has internal padding that, on these small fixed-size buttons,
        // clipped the glyph down to nothing (that's why they looked empty).
        const QString iconBtnQss =
            QStringLiteral("QPushButton{background:transparent;border:none;padding:0;margin:0;"
                           "color:#94a3b8;font-size:14px;}"
                           "QPushButton:hover{color:#e2e8f0;}");

        // 0x25BE ▾ expanded / 0x25B8 ▸ collapsed, 0x270E ✎ rename, 0x2715 ✕ delete,
        // 0x25C9 ◉ isolate ("eye").
        auto *chevron = new QPushButton(QString(QChar(collapsed ? 0x25B8 : 0x25BE)));
        chevron->setFixedSize(22, 22);
        chevron->setStyleSheet(
            QStringLiteral("QPushButton{background:transparent;border:none;padding:0;margin:0;"
                           "color:#cbd5e1;font-size:13px;}"));
        connect(chevron, &QPushButton::clicked, this, [this, name]() {
            if (m_collapsedGroups.contains(name)) {
                m_collapsedGroups.remove(name);
            } else {
                m_collapsedGroups.insert(name);
            }
            rebuildGroups();
        });

        auto *nameLabel = new QLabel(name);
        nameLabel->setStyleSheet(QStringLiteral("font-weight:bold;color:#e2e8f0;"));

        auto *countLabel = new QLabel();
        countLabel->setStyleSheet(QStringLiteral("color:#94a3b8;font-size:11px;"));
        m_groupCountLabels.insert(name, countLabel);

        auto *editBtn = new QPushButton(QString(QChar(0x270E)));
        editBtn->setFixedSize(24, 22);
        editBtn->setToolTip(tr("Renombrar grupo"));
        editBtn->setStyleSheet(iconBtnQss);
        connect(editBtn, &QPushButton::clicked, this, [this, name]() { renameGroup(name); });

        auto *delBtn = new QPushButton(QString(QChar(0x2715)));
        delBtn->setFixedSize(24, 22);
        delBtn->setToolTip(tr("Eliminar grupo"));
        delBtn->setStyleSheet(iconBtnQss);
        connect(delBtn, &QPushButton::clicked, this, [this, name]() {
            m_groups.remove(name);
            m_collapsedGroups.remove(name);
            if (m_isolatedGroup == name) {
                m_isolatedGroup.clear();
                relayout();
            }
            saveGroups();
            rebuildGroups();
        });

        const bool isolated = (m_isolatedGroup == name);
        auto *eyeBtn = new QPushButton(QString(QChar(0x25C9)));
        eyeBtn->setFixedSize(24, 22);
        eyeBtn->setToolTip(isolated ? tr("Mostrar todos los teléfonos")
                                    : tr("Ver solo los teléfonos de este grupo"));
        eyeBtn->setStyleSheet(isolated
            ? QStringLiteral("QPushButton{background:transparent;border:none;padding:0;margin:0;"
                             "color:#3b9dff;font-size:14px;}")
            : iconBtnQss);
        connect(eyeBtn, &QPushButton::clicked, this, [this, name]() {
            // Toggle the isolate view: hide every tile not in this group.
            m_isolatedGroup = (m_isolatedGroup == name) ? QString() : name;
            relayout();
            rebuildGroups();    // refresh the eye highlight
        });

        header->addWidget(chevron, 0);
        header->addWidget(nameLabel, 0);
        header->addWidget(countLabel, 0);
        header->addStretch(1);
        header->addWidget(editBtn, 0);
        header->addWidget(delBtn, 0);
        header->addWidget(eyeBtn, 0);
        cardLay->addLayout(header);

        if (!collapsed) {
            // ---- "Seleccionar todo" ----
            auto *selAll = new QCheckBox(tr("Seleccionar todo"));
            selAll->setTristate(true);
            selAll->setStyleSheet(QStringLiteral("QCheckBox{color:#60a5fa;font-size:12px;}"));
            m_groupSelectAll.insert(name, selAll);
            connect(selAll, &QCheckBox::clicked, this, [this, name]() {
                const QStringList mem = m_groups.value(name);
                bool allIn = !mem.isEmpty();
                for (const QString &s : mem) {
                    if (!m_selectedSerials.contains(s)) {
                        allIn = false;
                        break;
                    }
                }
                if (allIn) {
                    for (const QString &s : mem) {
                        m_selectedSerials.remove(s);
                    }
                } else {
                    for (const QString &s : mem) {
                        m_selectedSerials.insert(s);
                    }
                }
                updateSelectorStyles();
                updateTileSelectionStyles();
            });
            cardLay->addWidget(selAll);

            // ---- Member chips ----
            auto *chipsHost = new QWidget;
            auto *chipsGrid = new QGridLayout(chipsHost);
            chipsGrid->setContentsMargins(0, 0, 0, 0);
            chipsGrid->setSpacing(4);
            chipsGrid->setAlignment(Qt::AlignLeft);

            QHash<QString, QPushButton *> chips;
            const int cols = 5;
            int i = 0;
            for (const QString &serial : members) {
                auto *chip = new QPushButton(chipLabelFor(serial));
                chip->setCheckable(true);
                chip->setFixedSize(34, 26);
                connect(chip, &QPushButton::clicked, this,
                        [this, serial]() { toggleSelection(serial); });
                chipsGrid->addWidget(chip, i / cols, i % cols);
                chips.insert(serial, chip);
                ++i;
            }
            m_groupChips.insert(name, chips);
            cardLay->addWidget(chipsHost);
        }

        m_groupsLayout->addWidget(card);
    }

    updateGroupStyles();
}

void FarmWindow::updateGroupStyles()
{
    for (auto git = m_groups.begin(); git != m_groups.end(); ++git) {
        const QString &name = git.key();
        const QStringList &members = git.value();

        int selectedCount = 0;
        for (const QString &s : members) {
            if (m_selectedSerials.contains(s)) {
                ++selectedCount;
            }
        }

        if (QLabel *count = m_groupCountLabels.value(name, nullptr)) {
            count->setText(QStringLiteral("(%1 / %2)").arg(selectedCount).arg(members.size()));
        }
        if (QCheckBox *selAll = m_groupSelectAll.value(name, nullptr)) {
            QSignalBlocker block(selAll);
            selAll->setCheckState(members.isEmpty() || selectedCount == 0 ? Qt::Unchecked
                                  : selectedCount == members.size()       ? Qt::Checked
                                                                          : Qt::PartiallyChecked);
        }
        const QHash<QString, QPushButton *> chips = m_groupChips.value(name);
        for (auto cit = chips.begin(); cit != chips.end(); ++cit) {
            const bool sel = m_selectedSerials.contains(cit.key());
            const bool mirroring = m_tiles.contains(cit.key());
            cit.value()->setChecked(sel);
            cit.value()->setStyleSheet(
                QStringLiteral("QPushButton{background:%1;border:1px solid %2;border-radius:4px;"
                               "color:#e2e8f0;font-size:11px;}")
                    .arg(sel ? QStringLiteral("#2563eb") : QStringLiteral("#1c2436"),
                         mirroring ? QStringLiteral("#22c55e") : QStringLiteral("#2a344a")));
        }
    }
}

void FarmWindow::renameGroup(const QString &name)
{
    bool ok = false;
    const QString newName = QInputDialog::getText(this, tr("Renombrar grupo"),
                                                  tr("Nuevo nombre:"), QLineEdit::Normal, name, &ok)
                                .trimmed();
    if (!ok || newName.isEmpty() || newName == name) {
        return;
    }
    const QStringList serials = m_groups.value(name);
    m_groups.remove(name);
    if (m_collapsedGroups.remove(name)) {
        m_collapsedGroups.insert(newName);
    }
    if (m_isolatedGroup == name) {
        m_isolatedGroup = newName;
    }
    m_groups.insert(newName, serials);
    saveGroups();
    rebuildGroups();
}

void FarmWindow::loadGroups()
{
    QSettings settings(QStringLiteral("ZamiApp"), QStringLiteral("AdbDeviceFarm"));
    settings.beginGroup(QStringLiteral("farm_groups"));
    // New scheme: an explicit __names__ list (so empty groups persist) + serials
    // stored by index (immune to group names containing '/').
    const QStringList names = settings.value(QStringLiteral("__names__")).toStringList();
    if (!names.isEmpty()) {
        for (int i = 0; i < names.size(); ++i) {
            m_groups.insert(names.at(i),
                            settings.value(QStringLiteral("serials_%1").arg(i)).toStringList());
        }
    } else {
        // Legacy scheme: each child key is a group name -> its serials.
        const QStringList keys = settings.childKeys();
        for (const QString &name : keys) {
            if (name == QLatin1String("__names__")) {
                continue;
            }
            m_groups.insert(name, settings.value(name).toStringList());
        }
    }
    settings.endGroup();
}

void FarmWindow::saveGroups()
{
    QSettings settings(QStringLiteral("ZamiApp"), QStringLiteral("AdbDeviceFarm"));
    settings.beginGroup(QStringLiteral("farm_groups"));
    settings.remove(QString());    // wipe both legacy and prior keys
    QStringList names;
    for (auto it = m_groups.begin(); it != m_groups.end(); ++it) {
        names << it.key();
    }
    settings.setValue(QStringLiteral("__names__"), names);
    for (int i = 0; i < names.size(); ++i) {
        settings.setValue(QStringLiteral("serials_%1").arg(i), m_groups.value(names.at(i)));
    }
    settings.endGroup();
}

void FarmWindow::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    scheduleRelayout();
}

bool FarmWindow::eventFilter(QObject *watched, QEvent *event)
{
    // The grid's available width changes when the host panel shows/hides or the
    // window resizes; reflow the columns whenever the viewport is resized.
    if (m_scroll && watched == m_scroll->viewport() && event->type() == QEvent::Resize) {
        scheduleRelayout();
        return QWidget::eventFilter(watched, event);
    }

    // Double-click on selector buttons to open in host mode
    if (qobject_cast<QPushButton *>(watched) && event->type() == QEvent::MouseButtonDblClick) {
        auto *button = qobject_cast<QPushButton *>(watched);
        if (button && button->property("deviceSerial").isValid()) {
            const QString serial = button->property("deviceSerial").toString();
            onTileDoubleClicked(serial);
            return true;
        }
    }

    // Drag selection on the selector grid
    if (watched == m_selectorGridHost) {
        switch (event->type()) {
        case QEvent::MouseButtonPress: {
            auto *me = static_cast<QMouseEvent *>(event);
            if (me->button() == Qt::LeftButton) {
                m_selectorRubberOrigin = me->position().toPoint();
                m_selectorDragging = false;
                // Store current selection to restore if it's just a click
                m_selectorPreDragSelection = m_selectedSerials;
            }
            break;
        }
        case QEvent::MouseMove: {
            auto *me = static_cast<QMouseEvent *>(event);
            if (me->buttons() & Qt::LeftButton) {
                const QPoint p = me->position().toPoint();
                if (!m_selectorDragging && (p - m_selectorRubberOrigin).manhattanLength() > 6) {
                    m_selectorDragging = true;
                    m_selectorRubberBand->setGeometry(QRect(m_selectorRubberOrigin, QSize()));
                    m_selectorRubberBand->show();
                    m_selectorRubberBand->raise();
                    m_selectorGridHost->setCursor(Qt::ClosedHandCursor);
                    // Clear previous selection when starting a new drag
                    m_selectedSerials.clear();
                    m_selectorCurrentDragSelection.clear();
                    updateSelectorStyles();
                    updateTileSelectionStyles();
                }
                if (m_selectorDragging) {
                    m_selectorRubberBand->setGeometry(QRect(m_selectorRubberOrigin, p).normalized());
                    // Build new selection based on rubber band
                    QSet<QString> newSelection;
                    QRect rubberRect = m_selectorRubberBand->geometry();
                    for (auto it = m_selectorButtons.begin(); it != m_selectorButtons.end(); ++it) {
                        QPushButton *btn = it.value();
                        if (rubberRect.intersects(btn->geometry())) {
                            newSelection.insert(it.key());
                        }
                    }
                    // Only update if selection changed
                    if (newSelection != m_selectorCurrentDragSelection) {
                        m_selectorCurrentDragSelection = newSelection;
                        m_selectedSerials = newSelection;
                        updateSelectorStyles();
                        updateTileSelectionStyles();
                    }
                }
            }
            break;
        }
        case QEvent::MouseButtonRelease: {
            auto *me = static_cast<QMouseEvent *>(event);
            if (me->button() == Qt::LeftButton) {
                if (m_selectorDragging) {
                    m_selectorRubberBand->hide();
                    m_selectorDragging = false;
                    m_selectorCurrentDragSelection.clear();
                } else {
                    // Single click - toggle the clicked button
                    const QString serial = selectorButtonAt(me->position().toPoint());
                    if (!serial.isEmpty()) {
                        toggleSelection(serial);
                    }
                }
                m_selectorGridHost->setCursor(Qt::PointingHandCursor);
                m_selectorPreDragSelection.clear();
            }
            break;
        }
        default:
            break;
        }
    }

    // Marquee selection on the grid background (tiles are mouse-transparent).
    if (watched == m_gridHost) {
        switch (event->type()) {
        case QEvent::MouseButtonPress: {
            auto *me = static_cast<QMouseEvent *>(event);
            if (me->button() == Qt::LeftButton) {
                m_rubberOrigin = me->position().toPoint();
                m_dragging = false;
                // If the press lands on an already-selected tile, dragging carries
                // the whole selection (GenFarmer-style chip) instead of marquee-ing.
                const QString hit = tileAt(me->position().toPoint());
                m_selDragArmed = !hit.isEmpty() && m_selectedSerials.contains(hit);
                m_selDragging = false;
            } else if (me->button() == Qt::RightButton) {
                // Check if we have multiple selected devices
                if (!m_selectedSerials.isEmpty()) {
                    showMultiSelectContextMenu(me->globalPosition().toPoint());
                } else {
                    const QString serial = tileAt(me->position().toPoint());
                    if (!serial.isEmpty()) {
                        showTileContextMenu(serial, me->globalPosition().toPoint());
                    }
                }
            }
            break;
        }
        case QEvent::MouseMove: {
            auto *me = static_cast<QMouseEvent *>(event);
            if (me->buttons() & Qt::LeftButton) {
                const QPoint p = me->position().toPoint();
                // Carrying a selection: skip the marquee (the chip is driven by the
                // cursor-badge timer while any device is selected).
                if (m_selDragArmed) {
                    if (!m_selDragging && (p - m_rubberOrigin).manhattanLength() > 6) {
                        m_selDragging = true;
                    }
                    break;
                }
                if (!m_dragging && (p - m_rubberOrigin).manhattanLength() > 6) {
                    m_dragging = true;
                    m_rubberBand->setGeometry(QRect(m_rubberOrigin, QSize()));
                    m_rubberBand->show();
                    m_rubberBand->raise();
                }
                if (m_dragging) {
                    m_rubberBand->setGeometry(QRect(m_rubberOrigin, p).normalized());

                    // Update preview of tiles under rubber band
                    QSet<QString> newTilesUnderRubber;
                    const QRect rubberRect = m_rubberBand->geometry();
                    for (auto it = m_tiles.begin(); it != m_tiles.end(); ++it) {
                        if (it.value()->isVisible() && it.value()->geometry().intersects(rubberRect)) {
                            newTilesUnderRubber.insert(it.key());
                        }
                    }

                    // Only update if changed
                    if (newTilesUnderRubber != m_tilesUnderRubber) {
                        // Remove preview from tiles no longer under rubber
                        for (const QString &serial : m_tilesUnderRubber) {
                            if (!newTilesUnderRubber.contains(serial)) {
                                if (DeviceTile *tile = m_tiles.value(serial, nullptr)) {
                                    tile->setSelectionPreview(false);
                                }
                                // Remove preview from selector button
                                if (m_selectorButtonsInPreview.contains(serial)) {
                                    updateSelectorButtonStyle(serial, false);
                                    m_selectorButtonsInPreview.remove(serial);
                                }
                            }
                        }
                        // Add preview to new tiles under rubber
                        for (const QString &serial : newTilesUnderRubber) {
                            if (!m_tilesUnderRubber.contains(serial)) {
                                if (DeviceTile *tile = m_tiles.value(serial, nullptr)) {
                                    tile->setSelectionPreview(true);
                                }
                                // Add preview to selector button
                                if (m_selectorButtons.contains(serial)) {
                                    updateSelectorButtonStyle(serial, true);
                                    m_selectorButtonsInPreview.insert(serial);
                                }
                            }
                        }
                        m_tilesUnderRubber = newTilesUnderRubber;
                    }
                }
            }
            break;
        }
        case QEvent::MouseButtonRelease: {
            auto *me = static_cast<QMouseEvent *>(event);
            if (me->button() != Qt::LeftButton) {
                break;
            }
            if (m_selDragArmed) {
                m_selDragArmed = false;
                if (m_selDragging) {
                    // It was a real carry-drag: keep the selection intact (don't
                    // treat the release as a click/toggle). The chip stays visible
                    // via the timer while devices remain selected.
                    m_selDragging = false;
                    break;
                }
                // Otherwise it was a plain click on a selected tile -> fall through
                // to the normal toggle handling below.
            }
            if (m_dragging) {
                const QRect r = m_rubberBand->geometry();
                m_rubberBand->hide();
                m_dragging = false;

                // Clear preview from all tiles
                for (const QString &serial : m_tilesUnderRubber) {
                    if (DeviceTile *tile = m_tiles.value(serial, nullptr)) {
                        tile->setSelectionPreview(false);
                    }
                }
                m_tilesUnderRubber.clear();

                // Clear preview from all selector buttons
                for (const QString &serial : m_selectorButtonsInPreview) {
                    updateSelectorButtonStyle(serial, false);
                }
                m_selectorButtonsInPreview.clear();

                applyRubberSelection(r, me->modifiers().testFlag(Qt::ControlModifier));
            } else {
                const QString serial = tileAt(me->position().toPoint());
                if (!serial.isEmpty()) {
                    onTileClicked(serial);    // toggle this tile's selection
                } else if (!m_selectedSerials.isEmpty()) {
                    // Click on empty background clears the current selection.
                    clearDeviceSelection();
                }
            }
            break;
        }
        case QEvent::MouseButtonDblClick: {
            auto *me = static_cast<QMouseEvent *>(event);
            const QString serial = tileAt(me->position().toPoint());
            if (!serial.isEmpty()) {
                onTileDoubleClicked(serial);
            }
            break;
        }
        default:
            break;
        }
    }
    return QWidget::eventFilter(watched, event);
}

void FarmWindow::refreshDevices()
{
    m_adb.execute("", QStringList() << "devices");
}

void FarmWindow::mirrorAll()
{
    if (m_available.isEmpty()) {
        m_statusBar->setText(tr("No devices — press Refresh first."));
        return;
    }
    // Queue every device that hasn't started connecting yet
    // (tiles may exist as placeholders, so check if already connected via IDeviceManage)
    for (const QString &serial : m_available) {
        bool alreadyConnected = qsc::IDeviceManage::getInstance().getDevice(serial) != nullptr;
        if (!alreadyConnected && !m_connecting.contains(serial) && !m_pending.contains(serial)) {
            m_pending.append(serial);
        }
    }
    pumpConnectQueue();
}

void FarmWindow::stopAll()
{
    m_pending.clear();
    m_connecting.clear();
    qsc::IDeviceManage::getInstance().disconnectAllDevice();
}

void FarmWindow::pumpConnectQueue()
{
    while (m_connecting.size() < kMaxConcurrent && !m_pending.isEmpty()) {
        const QString serial = m_pending.takeFirst();
        // Skip if already connected (not just if placeholder exists)
        bool alreadyConnected = qsc::IDeviceManage::getInstance().getDevice(serial) != nullptr;
        if (alreadyConnected || m_connecting.contains(serial)) {
            continue;
        }
        m_connecting.insert(serial);
        if (!startConnect(serial)) {
            m_connecting.remove(serial);    // immediate failure; slot will keep filling
        }
    }
    updateConnectStatus();
}

void FarmWindow::updateConnectStatus()
{
    const int mirroring = static_cast<int>(m_tiles.size()) - static_cast<int>(m_connecting.size());
    m_statusBar->setText(tr("%1 mirroring · %2 connecting · %3 queued")
                             .arg(std::max(0, mirroring))
                             .arg(static_cast<int>(m_connecting.size()))
                             .arg(static_cast<int>(m_pending.size())));
}

namespace {
// Parse a dotted IPv4 string into a 32-bit value (big-endian). Returns false on
// any malformed octet so the caller can report the error.
bool parseIpv4(const QString &s, quint32 &out)
{
    const QStringList parts = s.split('.');
    if (parts.size() != 4) {
        return false;
    }
    quint32 v = 0;
    for (const QString &p : parts) {
        bool ok = false;
        const int n = p.toInt(&ok);
        if (!ok || n < 0 || n > 255) {
            return false;
        }
        v = (v << 8) | static_cast<quint32>(n);
    }
    out = v;
    return true;
}

QString u32ToIp(quint32 v)
{
    return QStringLiteral("%1.%2.%3.%4")
        .arg((v >> 24) & 0xFF)
        .arg((v >> 16) & 0xFF)
        .arg((v >> 8) & 0xFF)
        .arg(v & 0xFF);
}
}    // namespace

QStringList FarmWindow::expandWifiTargets(const QString &text) const
{
    const QString trimmed = text.trimmed();
    QStringList out;
    if (trimmed.isEmpty()) {
        return out;
    }

    QString portStr = QStringLiteral("5555");    // adb-over-WiFi default
    const int dash = trimmed.indexOf('-');

    // No dash → a single host (the original behaviour).
    if (dash < 0) {
        QString t = trimmed;
        if (!t.contains(':')) {
            t += ':' + portStr;
        }
        out << t;
        return out;
    }

    // Range "START-END". START is a full IPv4 (optionally with :port); END is
    // either a full IPv4 or just the final octet (e.g. "192.168.1.50-100").
    QString left = trimmed.left(dash).trimmed();
    QString right = trimmed.mid(dash + 1).trimmed();
    if (left.contains(':')) {
        portStr = left.section(':', 1, 1);
        left = left.section(':', 0, 0);
    }
    if (right.contains(':')) {
        right = right.section(':', 0, 0);
    }

    quint32 start = 0;
    if (!parseIpv4(left, start)) {
        return out;    // bad start address → empty list, caller reports it
    }
    quint32 end = 0;
    if (right.contains('.')) {
        if (!parseIpv4(right, end)) {
            return out;
        }
    } else {
        bool ok = false;
        const int last = right.toInt(&ok);
        if (!ok || last < 0 || last > 255) {
            return out;
        }
        end = (start & 0xFFFFFF00u) | static_cast<quint32>(last);
    }
    if (end < start) {
        return out;
    }
    // Cap the sweep so a typo (e.g. .1-254 across subnets) can't spawn thousands
    // of adb processes.
    constexpr quint32 kMaxRange = 256;
    if (end - start + 1 > kMaxRange) {
        end = start + kMaxRange - 1;
    }
    for (quint32 v = start; v <= end; ++v) {
        out << u32ToIp(v) + ':' + portStr;
    }
    return out;
}

void FarmWindow::connectWifi()
{
    const QString a = m_octA->text().trimmed();
    const QString b = m_octB->text().trimmed();
    const QString c = m_octC->text().trimmed();
    const QString d1 = m_octStart->text().trimmed();
    const QString d2 = m_octEnd->text().trimmed();
    QString port = m_portEdit->text().trimmed();
    if (port.isEmpty()) {
        port = QStringLiteral("5555");
    }
    if (a.isEmpty() || b.isEmpty() || c.isEmpty() || d1.isEmpty()) {
        m_statusBar->setText(tr("Completa la dirección IP inicial."));
        return;
    }
    // Build "a.b.c.d1:port" (single) or "a.b.c.d1:port-d2" (range) and let
    // expandWifiTargets() validate and expand it.
    QString spec = QStringLiteral("%1.%2.%3.%4:%5").arg(a, b, c, d1, port);
    if (!d2.isEmpty() && d2 != d1) {
        spec += '-' + d2;
    }
    const QStringList targets = expandWifiTargets(spec);
    if (targets.isEmpty()) {
        m_statusBar->setText(tr("Dirección IP o rango inválido."));
        return;
    }
    if (targets.size() == 1) {
        // Single host: keep the lightweight path through the shared AdbProcess.
        m_adb.execute("", QStringList() << "connect" << targets.first());
        m_statusBar->setText(tr("connecting to %1…").arg(targets.first()));
        return;
    }
    // Range sweep: the shared AdbProcess runs one command at a time, so probe the
    // whole range with an independent, throttled pool of `adb connect` processes.
    m_wifiConnectQueue = targets;
    m_wifiConnectTotal = static_cast<int>(targets.size());
    m_wifiConnectDone = 0;
    m_statusBar->setText(tr("scanning %1 addresses…").arg(m_wifiConnectTotal));
    pumpWifiConnect();
}

void FarmWindow::pumpWifiConnect()
{
    constexpr int kMaxWifiConnect = 16;    // bound the parallel adb-connect fan-out
    const QString adb = resolveAdbPath();
    while (m_wifiConnectActive < kMaxWifiConnect && !m_wifiConnectQueue.isEmpty()) {
        const QString target = m_wifiConnectQueue.takeFirst();
        auto *proc = new QProcess(this);
        ++m_wifiConnectActive;
        auto done = std::make_shared<bool>(false);
        auto finish = [this, proc, done]() {
            if (*done) {
                return;
            }
            *done = true;
            proc->deleteLater();
            --m_wifiConnectActive;
            ++m_wifiConnectDone;
            if (m_wifiConnectQueue.isEmpty() && m_wifiConnectActive == 0) {
                m_statusBar->setText(
                    tr("WiFi scan done (%1 probed) — refreshing.").arg(m_wifiConnectTotal));
                refreshDevices();    // pick up everything that actually connected
            } else {
                m_statusBar->setText(tr("WiFi scan: %1 / %2 probed…")
                                         .arg(m_wifiConnectDone)
                                         .arg(m_wifiConnectTotal));
                pumpWifiConnect();
            }
        };
        connect(proc, &QProcess::finished, this,
                [finish](int, QProcess::ExitStatus) { finish(); });
        connect(proc, &QProcess::errorOccurred, this,
                [finish](QProcess::ProcessError) { finish(); });
        // adb connect blocks on its own timeout against a dead host; this is just a
        // backstop so a wedged process never strands a slot forever.
        QTimer::singleShot(10000, proc, [finish]() { finish(); });
        proc->start(adb, QStringList() << "connect" << target);
    }
}

void FarmWindow::enableWifiSelected()
{
    if (m_selected.isEmpty()) {
        m_statusBar->setText(tr("Select a USB device tile first."));
        return;
    }
    m_wifiSerial = m_selected;
    m_adb.execute(m_wifiSerial, QStringList() << "tcpip" << "5555");
    m_statusBar->setText(tr("enabling TCP/IP on %1…").arg(m_wifiSerial));
}

void FarmWindow::onAdbResult(qsc::AdbProcess::ADB_EXEC_RESULT result)
{
    if (result == qsc::AdbProcess::AER_ERROR_MISSING_BINARY) {
        m_statusBar->setText(tr("adb not found"));
        return;
    }
    if (result != qsc::AdbProcess::AER_SUCCESS_EXEC && result != qsc::AdbProcess::AER_ERROR_EXEC) {
        return;
    }

    const QStringList args = m_adb.arguments();
    const bool ok = (result == qsc::AdbProcess::AER_SUCCESS_EXEC);

    if (args.contains("devices")) {
        if (ok) {
            m_available = m_adb.getDevicesSerialFromStdOut();
            rebuildNumbering();
            rebuildSelector();
            m_statusBar->setText(
                tr("%1 device(s) detected").arg(static_cast<int>(m_available.size())));
            // First enumeration after launch: auto-connect everything ("Mirror All")
            // and let the splash know it can close.
            if (m_autoMirrorPending) {
                m_autoMirrorPending = false;
                emit firstDevicesReady();
                if (!m_available.isEmpty()) {
                    mirrorAll();
                }
            }
        }
    } else if (args.contains("tcpip")) {
        if (ok) {
            m_statusBar->setText(tr("reading WiFi IP of %1…").arg(m_wifiSerial));
            m_adb.execute(m_wifiSerial, QStringList() << "shell" << "ifconfig" << "wlan0");
        } else {
            m_statusBar->setText(tr("tcpip failed on %1").arg(m_wifiSerial));
        }
    } else if (args.contains("ifconfig") && args.contains("wlan0")) {
        const QString ip = ok ? m_adb.getDeviceIPFromStdOut() : QString();
        if (ip.isEmpty()) {
            m_adb.execute(m_wifiSerial, QStringList() << "shell" << "ip -o a");
        } else {
            m_adb.execute("", QStringList() << "connect" << (ip + ":5555"));
            m_statusBar->setText(tr("connecting to %1:5555…").arg(ip));
        }
    } else if (args.contains("ip -o a")) {
        const QString ip = ok ? m_adb.getDeviceIPByIpFromStdOut() : QString();
        if (ip.isEmpty()) {
            m_statusBar->setText(tr("Could not read WiFi IP — connect manually."));
        } else {
            m_adb.execute("", QStringList() << "connect" << (ip + ":5555"));
            m_statusBar->setText(tr("connecting to %1:5555…").arg(ip));
        }
    } else if (args.contains("connect")) {
        m_wifiSerial.clear();
        m_statusBar->setText(tr("WiFi connect finished — refreshing."));
        refreshDevices();
    }
}

bool FarmWindow::startConnect(const QString &serial)
{
    DeviceTile *tile = ensureTile(serial);
    tile->setStatusText(tr("connecting…"));
    tile->setLoading(true);  // Start loading animation

    // Normalize resolution/density BEFORE scrcpy captures, so every phone streams
    // and accepts control at the same coordinate space (mixed native resolutions
    // otherwise make broadcast input land in the wrong place on some models).
    // This MUST NOT block the GUI thread: with "Mirror All" the queue calls
    // startConnect() for several devices back-to-back, and a synchronous adb shell
    // per device froze the window ("Not Responding"). Run it async, then continue
    // the actual connection in finishConnect() once normalization completes.
    const QString adb = resolveAdbPath();
    auto *proc = new QProcess(this);
    auto done = std::make_shared<bool>(false);
    auto proceed = [this, serial, proc, done]() {
        if (*done) {
            return;
        }
        *done = true;
        proc->deleteLater();
        finishConnect(serial);
    };
    connect(proc, &QProcess::finished, this,
            [proceed](int, QProcess::ExitStatus) { proceed(); });
    connect(proc, &QProcess::errorOccurred, this,
            [proceed](QProcess::ProcessError) { proceed(); });
    // Safety net: never let a stuck adb shell strand a connecting slot forever.
    QTimer::singleShot(8000, proc, [proceed]() { proceed(); });
    proc->start(adb, {"-s", serial, "shell",
                      QStringLiteral("wm size %1 ; wm density %2")
                          .arg(QLatin1String(kNormalizedSize), QLatin1String(kNormalizedDensity))});
    return true;
}

void FarmWindow::finishConnect(const QString &serial)
{
    // The device may have been cancelled/disconnected while normalizing.
    if (!m_connecting.contains(serial)) {
        return;
    }
    DeviceTile *tile = m_tiles.value(serial, nullptr);
    if (!tile) {
        m_connecting.remove(serial);
        pumpConnectQueue();
        return;
    }

    qsc::DeviceParams params;
    params.serial = serial;
    params.serverLocalPath = serverPath();
    params.maxSize = m_maxSize;
    params.bitRate = m_bitRate;
    params.maxFps = m_maxFps;
    params.useReverse = true;
    params.stayAwake = true;
    params.renderExpiredFrames = false;
    // Unique scid AND unique reverse port per device: the core otherwise shares
    // port 27183, so concurrent connections collide and only one succeeds.
    params.scid = QRandomGenerator::global()->bounded(1, 10000) & 0x7FFFFFFF;
    params.localPort = static_cast<quint16>(kBasePort + (m_portSeq++ % 900));

    if (!qsc::IDeviceManage::getInstance().connectDevice(params)) {
        m_statusBar->setText(tr("connect failed: %1").arg(serial));
        tile->setLoading(false);
        m_connecting.remove(serial);
        removeTile(serial);
        pumpConnectQueue();    // free the slot and keep the queue moving
        return;
    }
    // Success: onDeviceConnected() will clear m_connecting and pump the queue.
}

void FarmWindow::onDeviceConnected(bool success, const QString &serial, const QString &deviceName,
                                   const QSize &size)
{
    Q_UNUSED(size);
    m_connecting.remove(serial);
    m_reloading.remove(serial);  // Clear reloading flag

    if (!success) {
        DeviceTile *tile = m_tiles.value(serial, nullptr);
        if (tile) {
            tile->setLoading(false);
        }
        removeTile(serial);
        pumpConnectQueue();    // free the slot; start the next queued device
        return;
    }

    DeviceTile *tile = ensureTile(serial);
    tile->setModel(deviceName);
    tile->setStatusText(tr("connected"));
    tile->setLoading(false);  // Stop loading animation

    auto device = qsc::IDeviceManage::getInstance().getDevice(serial);
    if (device) {
        device->setUserData(static_cast<void *>(tile));
        device->registerDeviceObserver(tile);
    }

    // Placeholders are already created/sorted/laid out at device enumeration, so a
    // connecting device keeps its slot — no full rebuildNumbering()/relayout() per
    // connect (that was O(n) work per device → O(n²) jank during "Mirror All").
    // Only rebuild if this device wasn't part of the enumerated set (e.g. a WiFi
    // device that appeared on its own).
    if (!m_order.contains(serial)) {
        rebuildNumbering();
    }

    updateSelectorButtonStyle(serial);    // mark just this device as mirroring (O(1))

    // Auto-install 333Farmer Helper APK if not already installed
    checkAndInstallHelperApk(serial);

    pumpConnectQueue();    // a slot just freed up; start the next queued device
}

void FarmWindow::onDeviceDisconnected(const QString &serial)
{
    auto device = qsc::IDeviceManage::getInstance().getDevice(serial);
    DeviceTile *tile = m_tiles.value(serial, nullptr);
    if (device && tile) {
        device->deRegisterDeviceObserver(tile);
    }
    if (m_focusSerial == serial) {
        m_focusPanel->detach();
        m_focusPanel->hide();
        m_focusSerial.clear();
    }
    m_connecting.remove(serial);

    // Don't remove tile if it's being reloaded
    if (!m_reloading.contains(serial)) {
        removeTile(serial);
    }

    updateSelectorStyles();    // device no longer mirroring
    pumpConnectQueue();
}

void FarmWindow::onTileClicked(const QString &serial)
{
    m_selected = serial;       // single target for "Enable WiFi (selected)"
    toggleSelection(serial);   // toggle multi-selection membership + highlight
}

void FarmWindow::onTileDoubleClicked(const QString &serial)
{
    if (!qsc::IDeviceManage::getInstance().getDevice(serial)) {
        return;    // not connected yet
    }

    // If double-clicking the currently focused device, close host mode
    if (m_focusSerial == serial && m_focusPanel->isVisible()) {
        m_focusPanel->detach();
        m_focusPanel->hide();
        if (DeviceTile *tile = m_tiles.value(serial, nullptr)) {
            tile->setUnderControl(false);
        }
        m_focusSerial.clear();
        relayout();
        return;
    }

    // Move the "under control" marker to the new device.
    if (!m_focusSerial.isEmpty() && m_focusSerial != serial && m_tiles.contains(m_focusSerial)) {
        m_tiles[m_focusSerial]->setUnderControl(false);
    }
    m_focusSerial = serial;

    m_focusPanel->setVisible(true);
    if (DeviceTile *tile = m_tiles.value(serial, nullptr)) {
        tile->setUnderControl(true);
    }
    relayout();
    m_focusPanel->showDevice(serial, serial);    // attach + schedule frame replay
    updateHostTargets();    // host broadcasts to itself + the current selection
}

QList<QString> FarmWindow::inputTargets(const QString &sourceSerial) const
{
    auto selectedInOrder = [this]() {
        QList<QString> targets;
        for (const QString &s : m_order) {
            if (m_selectedSerials.contains(s)) {
                targets.append(s);
            }
        }
        return targets;
    };

    // "Control All" broadcasts to the selection (or everything if none selected).
    if (m_groupMode) {
        return m_selectedSerials.isEmpty() ? m_order : selectedInOrder();
    }
    // Otherwise, interacting with a device that's part of a multi-selection
    // controls the whole selection (drag-select then control).
    if (m_selectedSerials.size() > 1 && m_selectedSerials.contains(sourceSerial)) {
        return selectedInOrder();
    }
    return QList<QString>{sourceSerial};
}

void FarmWindow::onTileMouse(const QString &serial, QMouseEvent *event)
{
    DeviceTile *src = m_tiles.value(serial, nullptr);
    if (!src) {
        return;
    }
    const QSize showSize = src->videoShowSize();
    const bool press = (event->type() == QEvent::MouseButtonPress);

    for (const QString &target : inputTargets(serial)) {
        auto device = qsc::IDeviceManage::getInstance().getDevice(target);
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
        DeviceTile *targetTile = m_tiles.value(target, nullptr);
        const QSize frameSize = targetTile ? targetTile->videoFrameSize() : src->videoFrameSize();
        device->mouseEvent(event, frameSize, showSize);
    }
}

void FarmWindow::onTileWheel(const QString &serial, QWheelEvent *event)
{
    DeviceTile *src = m_tiles.value(serial, nullptr);
    if (!src) {
        return;
    }
    const QSize showSize = src->videoShowSize();
    for (const QString &target : inputTargets(serial)) {
        auto device = qsc::IDeviceManage::getInstance().getDevice(target);
        if (!device) {
            continue;
        }
        DeviceTile *targetTile = m_tiles.value(target, nullptr);
        const QSize frameSize = targetTile ? targetTile->videoFrameSize() : src->videoFrameSize();
        device->wheelEvent(event, frameSize, showSize);
    }
}

void FarmWindow::onTileKey(const QString &serial, QKeyEvent *event)
{
    DeviceTile *src = m_tiles.value(serial, nullptr);
    if (!src) {
        return;
    }

    // Ctrl+V pastes the PC clipboard into the device (push host clipboard +
    // paste, like scrcpy). Forwarding the raw V keycode would instead paste the
    // *device's* own clipboard, which is why plain Ctrl+V never worked.
    if ((event->modifiers() & Qt::ControlModifier) && event->key() == Qt::Key_V) {
        if (event->type() == QEvent::KeyPress) {
            for (const QString &target : inputTargets(serial)) {
                auto device = qsc::IDeviceManage::getInstance().getDevice(target);
                if (device) {
                    device->setDeviceClipboard(true);
                }
            }
        }
        return;    // swallow the keycode so it isn't double-handled
    }

    const QSize showSize = src->videoShowSize();
    for (const QString &target : inputTargets(serial)) {
        auto device = qsc::IDeviceManage::getInstance().getDevice(target);
        if (!device) {
            continue;
        }
        DeviceTile *targetTile = m_tiles.value(target, nullptr);
        const QSize frameSize = targetTile ? targetTile->videoFrameSize() : src->videoFrameSize();
        device->keyEvent(event, frameSize, showSize);
    }
}

void FarmWindow::onTileReloadRequested(const QString &serial)
{
    // Mark as reloading to prevent tile removal in onDeviceDisconnected
    m_reloading.insert(serial);

    // Disconnect the device if connected
    auto device = qsc::IDeviceManage::getInstance().getDevice(serial);
    if (device) {
        DeviceTile *tile = m_tiles.value(serial, nullptr);
        if (tile) {
            device->deRegisterDeviceObserver(tile);
        }
        qsc::IDeviceManage::getInstance().disconnectDevice(serial);
    }

    // Remove from connecting/pending state
    m_connecting.remove(serial);
    m_pending.removeAll(serial);

    // Keep the tile and show loading animation
    DeviceTile *tile = m_tiles.value(serial, nullptr);
    if (tile) {
        tile->setModel("...");
        tile->setStatusText(tr("reconnecting…"));
        tile->setLoading(true);
    }

    // Add to pending queue and start connection
    m_pending.append(serial);
    pumpConnectQueue();
}

void FarmWindow::showTileContextMenu(const QString &serial, const QPoint &globalPos)
{
    QMenu menu(this);

    QAction *reloadAction = menu.addAction(tr("Recargar"));
    connect(reloadAction, &QAction::triggered, this, [this, serial]() {
        onTileReloadRequested(serial);
    });

    QAction *adbControllerAction = menu.addAction(tr("ADB Controller"));
    connect(adbControllerAction, &QAction::triggered, this, [this, serial]() {
        openAdbController(QStringList{serial});
    });

    QAction *restartAction = menu.addAction(tr("Reiniciar teléfono"));
    connect(restartAction, &QAction::triggered, this, [this, serial]() {
        restartDevice(serial);
    });

    menu.addSeparator();
    buildAddToGroupMenu(&menu, QStringList{serial});

    menu.exec(globalPos);
}

void FarmWindow::showMultiSelectContextMenu(const QPoint &globalPos)
{
    QMenu menu(this);

    const int count = m_selectedSerials.size();
    QAction *reloadAction = menu.addAction(tr("Recargar %1 dispositivos").arg(count));
    connect(reloadAction, &QAction::triggered, this, [this]() {
        for (const QString &serial : m_selectedSerials) {
            onTileReloadRequested(serial);
        }
    });

    QAction *wallpaperAction = menu.addAction(tr("Set Numbered Wallpaper"));
    connect(wallpaperAction, &QAction::triggered, this, &FarmWindow::setNumberedWallpapers);

    QAction *adbControllerAction = menu.addAction(tr("ADB Controller"));
    connect(adbControllerAction, &QAction::triggered, this, [this]() { openAdbController(); });

    QAction *restartAction = menu.addAction(tr("Reiniciar %1 teléfonos").arg(count));
    connect(restartAction, &QAction::triggered, this, [this]() {
        const QList<QString> serials = m_selectedSerials.values();
        for (const QString &serial : serials) {
            restartDevice(serial);
        }
    });

    menu.addSeparator();
    buildAddToGroupMenu(&menu, m_selectedSerials.values());

    menu.exec(globalPos);
}

void FarmWindow::onTileContextMenuRequested(const QString &serial, const QPoint &globalPos)
{
    // If we have multiple devices selected, show the multi-select menu
    if (!m_selectedSerials.isEmpty()) {
        showMultiSelectContextMenu(globalPos);
    } else {
        // Otherwise show the single-tile menu
        showTileContextMenu(serial, globalPos);
    }
}

void FarmWindow::setTileSize(int width)
{
    m_tileWidth = width;
    m_tileSizeValue->setText(QString::number(width));
    for (DeviceTile *tile : m_tiles) {
        tile->setTileWidth(width);
    }
    relayout();
}

void FarmWindow::setHostSize(int height)
{
    m_hostHeight = height;
    m_hostSizeValue->setText(QString::number(height));
    if (m_focusPanel) {
        m_focusPanel->setHostHeight(height);    // panel resizes; grid reflows via viewport filter
    }
}

void FarmWindow::setQuality(int maxSize)
{
    m_maxSize = static_cast<quint16>(maxSize);
    m_bitRate = static_cast<quint32>(maxSize) * 5000;    // ~4 Mbps at 800 px
    m_qualityValue->setText(QString::number(maxSize));
}

void FarmWindow::setFrameRate(int fps)
{
    m_maxFps = static_cast<quint32>(fps);
    m_fpsValue->setText(QString::number(fps));
}

void FarmWindow::setGroupMode(bool on)
{
    m_groupMode = on;
    updateHostTargets();
    m_statusBar->setText(on ? tr("Control All ON — host controls every device.")
                            : tr("Control All OFF — host controls the selection."));
}

void FarmWindow::setSmallViewControl(bool on)
{
    m_smallViewControl = on;
    for (DeviceTile *tile : m_tiles) {
        tile->setControllable(on);
    }
    m_statusBar->setText(on ? tr("Small-view control ON — tap a tile to control it.")
                            : tr("Small-view control OFF — drag to select on the grid."));
}

DeviceTile *FarmWindow::ensureTile(const QString &serial)
{
    auto it = m_tiles.find(serial);
    if (it != m_tiles.end()) {
        return it.value();
    }
    auto *tile = new DeviceTile(serial, m_gridHost);
    tile->setTileWidth(m_tileWidth);
    tile->setControllable(m_smallViewControl);
    // Per-tile control (active only when the tile is controllable; otherwise the
    // tile is mouse-transparent and the grid drives marquee selection instead).
    connect(tile, &DeviceTile::mouseInput, this, &FarmWindow::onTileMouse);
    connect(tile, &DeviceTile::wheelInput, this, &FarmWindow::onTileWheel);
    connect(tile, &DeviceTile::keyInput, this, &FarmWindow::onTileKey);
    connect(tile, &DeviceTile::doubleClicked, this, &FarmWindow::onTileDoubleClicked);
    connect(tile, &DeviceTile::reloadRequested, this, &FarmWindow::onTileReloadRequested);
    connect(tile, &DeviceTile::contextMenuRequested, this, &FarmWindow::onTileContextMenuRequested);
    m_tiles.insert(serial, tile);
    m_order.append(serial);
    scheduleRelayout();
    return tile;
}

void FarmWindow::removeTile(const QString &serial)
{
    // While a device is rebooting we keep its tile alive (showing "rebooting…")
    // even if a transient reconnect attempt fails — the poll loop owns its fate.
    if (m_rebootWait.contains(serial)) {
        DeviceTile *tile = m_tiles.value(serial, nullptr);
        if (tile) {
            tile->setStatusText(tr("rebooting…"));
            tile->setLoading(true);
        }
        m_connecting.remove(serial);
        return;
    }

    auto it = m_tiles.find(serial);
    if (it == m_tiles.end()) {
        return;
    }
    DeviceTile *tile = it.value();
    m_tiles.erase(it);
    m_order.removeAll(serial);
    if (m_selected == serial) {
        m_selected.clear();
    }
    tile->deleteLater();
    relayout();
}

void FarmWindow::relayout()
{
    if (!m_scroll) {
        return;
    }
    // Detach tiles without destroying them, and clear old stretches.
    while (QLayoutItem *item = m_grid->takeAt(0)) {
        delete item;
    }
    for (int c = 0; c <= 64; ++c) {
        m_grid->setColumnStretch(c, 0);
    }
    for (int r = 0; r <= 512; ++r) {
        m_grid->setRowStretch(r, 0);
    }

    // Group "isolate" view (the eye 👁): show only the chosen group's tiles, hide
    // the rest. Empty m_isolatedGroup → show everything.
    const bool isolate = !m_isolatedGroup.isEmpty() && m_groups.contains(m_isolatedGroup);
    QSet<QString> isoSet;
    if (isolate) {
        const QStringList mem = m_groups.value(m_isolatedGroup);
        for (const QString &s : mem) {
            isoSet.insert(s);
        }
    }

    QList<QString> visible;
    for (const QString &s : m_order) {
        DeviceTile *tile = m_tiles.value(s, nullptr);
        if (!tile) {
            continue;
        }
        if (isolate && !isoSet.contains(s)) {
            tile->hide();
        } else {
            visible.append(s);
        }
    }

    const int count = static_cast<int>(visible.size());
    if (count == 0) {
        return;
    }

    const int avail = m_scroll->viewport()->width() - 2 * kGridMargin;
    const int per = m_tileWidth + kGridSpacing;
    int cols = std::max(1, (avail + kGridSpacing) / per);
    cols = std::min(cols, count);

    for (int i = 0; i < count; ++i) {
        DeviceTile *tile = m_tiles.value(visible.at(i), nullptr);
        if (!tile) {
            continue;
        }
        tile->show();
        tile->setNumber(m_numbering.value(visible.at(i), i + 1));
        m_grid->addWidget(tile, i / cols, i % cols, Qt::AlignTop | Qt::AlignLeft);
    }
    const int rows = (count + cols - 1) / cols;
    m_grid->setColumnStretch(cols, 1);    // push tiles to the left
    m_grid->setRowStretch(rows, 1);       // push tiles to the top
}

void FarmWindow::scheduleRelayout()
{
    // Collapse a burst of relayout requests (many tiles connecting at once, rapid
    // resize events) into a single relayout on the next event-loop turn, so the
    // grid isn't rebuilt O(n) times during "Mirror All".
    if (m_relayoutPending) {
        return;
    }
    m_relayoutPending = true;
    QTimer::singleShot(0, this, [this]() {
        m_relayoutPending = false;
        relayout();
    });
}

QString FarmWindow::serverPath()
{
    QString path = QString::fromLocal8Bit(qgetenv("QTSCRCPY_SERVER_PATH"));
    QFileInfo info(path);
    if (path.isEmpty() || !info.isFile()) {
        path = QCoreApplication::applicationDirPath() + "/scrcpy-server";
    }
    return path;
}

void FarmWindow::openAdbController()
{
    QStringList serials;
    for (const QString &s : m_order) {
        if (m_selectedSerials.contains(s)) {
            serials << s;
        }
    }
    openAdbController(serials);
}

void FarmWindow::openAdbController(const QStringList &serials)
{
    if (serials.isEmpty()) {
        m_statusBar->setText(tr("No devices selected."));
        return;
    }

    auto *dialog = new AdbControllerDialog(serials, this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->exec();
}

void FarmWindow::openInstallApk(const QString &serial)
{
    // Install onto the broadcast set (host + selection / group), like the other
    // host-panel actions. Build numbered (serial, number) chips for the dialog.
    const QList<QString> targets = inputTargets(serial);
    QList<QPair<QString, int>> chips;
    for (const QString &t : targets) {
        chips.append({t, m_numbering.value(t, 0)});
    }

    InstallApkDialog dialog(chips, this);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    const QString apk = dialog.apkPath();
    int sent = 0;
    for (const QString &t : targets) {
        auto device = qsc::IDeviceManage::getInstance().getDevice(t);
        if (device) {
            device->installApkRequest(apk);
            ++sent;
        }
    }
    m_statusBar->setText(tr("Installing APK on %1 device(s)…").arg(sent));
}

void FarmWindow::openDevicesDialog()
{
    auto *dialog = new DevicesDialog(this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);

    QList<DeviceInfo> devices;
    for (const QString &serial : m_order) {
        if (!m_available.contains(serial)) {
            continue;
        }

        DeviceInfo info;
        info.serial = serial;
        info.index = m_numbering.value(serial, 0);
        info.connected = (qsc::IDeviceManage::getInstance().getDevice(serial) != nullptr);

        DeviceTile *tile = m_tiles.value(serial, nullptr);
        if (tile && !tile->model().isEmpty()) {
            info.model = tile->model();
        } else {
            info.model = serial.split(":").first();
        }
        info.ipPort = serial;

        devices.append(info);
    }

    dialog->setDevices(devices);

    connect(dialog, &DevicesDialog::refreshRequested, this, &FarmWindow::refreshDevices);
    connect(dialog, &DevicesDialog::restartAdbRequested, this, [this]() {
        m_adb.execute("", QStringList() << "kill-server");
        m_adb.execute("", QStringList() << "start-server");
        refreshDevices();
    });
    connect(dialog, &DevicesDialog::restartDeviceRequested, this, [this](const QString &serial) {
        restartDevice(serial);
    });
    connect(dialog, &DevicesDialog::connectDeviceRequested, this, [this](const QString &serial) {
        if (!m_pending.contains(serial) && !m_connecting.contains(serial)) {
            m_pending.append(serial);
            pumpConnectQueue();
        }
    });
    connect(dialog, &DevicesDialog::disconnectDeviceRequested, this, [this](const QString &serial) {
        qsc::IDeviceManage::getInstance().disconnectDevice(serial);
        removeTile(serial);
    });

    dialog->exec();
}

void FarmWindow::restartDevice(const QString &serial)
{
    // Reboot the phone but keep its tile on the grid: mark it reloading so
    // onDeviceDisconnected won't drop the tile when the connection dies, then
    // poll until the device boots back up and reconnect the mirror automatically.
    m_reloading.insert(serial);

    DeviceTile *tile = ensureTile(serial);
    tile->setStatusText(tr("rebooting…"));
    tile->setLoading(true);

    // Tear down the live mirror cleanly before rebooting.
    auto device = qsc::IDeviceManage::getInstance().getDevice(serial);
    if (device) {
        device->deRegisterDeviceObserver(tile);
        qsc::IDeviceManage::getInstance().disconnectDevice(serial);
    }
    m_connecting.remove(serial);
    m_pending.removeAll(serial);

    // Fire-and-forget so the UI thread never blocks on the reboot command.
    QProcess::startDetached(resolveAdbPath(), {"-s", serial, "reboot"});
    m_statusBar->setText(tr("rebooting %1…").arg(serial));

    // Wait a fixed grace period before polling: by then the phone is definitely
    // past shutdown, so a "boot_completed=1" reading means the *new* boot (not a
    // stale flag from before the reboot). We don't try to detect the "offline"
    // window — adb auto-reconnects WiFi devices and that window is easy to miss.
    m_rebootWait[serial] = 0;
    QTimer::singleShot(15000, this, [this, serial]() { pollRebootedDevice(serial); });
}

void FarmWindow::pollRebootedDevice(const QString &serial)
{
    // Aborted (tile gone / no longer tracked) — stop polling.
    if (!m_rebootWait.contains(serial) || !m_tiles.contains(serial)) {
        m_rebootWait.remove(serial);
        return;
    }

    // Mirror is live again — done.
    if (qsc::IDeviceManage::getInstance().getDevice(serial) != nullptr) {
        m_rebootWait.remove(serial);
        m_reloading.remove(serial);
        return;
    }

    constexpr int kMaxPolls = 60;          // ~3 min at 3s intervals (after the 15s grace)
    constexpr int kPollIntervalMs = 3000;

    int attempts = m_rebootWait.value(serial, 0);
    if (attempts >= kMaxPolls) {
        // Gave up waiting — drop the tile.
        m_rebootWait.remove(serial);     // must clear before removeTile (which guards on it)
        m_reloading.remove(serial);
        DeviceTile *tile = m_tiles.value(serial, nullptr);
        if (tile) {
            tile->setLoading(false);
        }
        removeTile(serial);
        m_statusBar->setText(tr("%1 did not come back after reboot").arg(serial));
        return;
    }
    m_rebootWait[serial] = attempts + 1;

    const QString adb = resolveAdbPath();

    // Runs an adb command asynchronously (never blocks the UI thread) and calls
    // cb with the trimmed stdout. A watchdog kills a process that hangs so the
    // chain always makes progress.
    auto runAdbAsync = [this](const QString &adbPath, const QStringList &args,
                              int timeoutMs, std::function<void(QString)> cb) {
        auto *proc = new QProcess(this);
        connect(proc, &QProcess::finished, this,
                [proc, cb](int, QProcess::ExitStatus) {
                    const QString out = QString::fromUtf8(proc->readAllStandardOutput()).trimmed();
                    proc->deleteLater();
                    cb(out);
                });
        connect(proc, &QProcess::errorOccurred, this,
                [proc, cb](QProcess::ProcessError) {
                    proc->deleteLater();
                    cb(QString());
                });
        auto *killer = new QTimer(proc);
        killer->setSingleShot(true);
        connect(killer, &QTimer::timeout, proc, [proc]() {
            if (proc->state() == QProcess::Running) {
                proc->kill();
            }
        });
        killer->start(timeoutMs);
        proc->start(adbPath, args);
    };

    // Schedules the next watchdog tick (only after the current async work ends).
    auto scheduleNext = [this, serial, kPollIntervalMs]() {
        QTimer::singleShot(kPollIntervalMs, this, [this, serial]() { pollRebootedDevice(serial); });
    };

    // Step 2: check whether Android finished booting, then reconnect if so.
    auto checkBoot = [this, serial, adb, runAdbAsync, scheduleNext]() {
        runAdbAsync(adb, {"-s", serial, "shell", "getprop", "sys.boot_completed"}, 4000,
                    [this, serial, scheduleNext](const QString &booted) {
            if (booted == QLatin1String("1")) {
                m_reloading.insert(serial);    // keep the tile through the reconnect
                if (!m_pending.contains(serial) && !m_connecting.contains(serial)) {
                    m_pending.append(serial);
                    pumpConnectQueue();
                }
                m_statusBar->setText(tr("%1 back online — reconnecting").arg(serial));
            } else {
                m_statusBar->setText(tr("waiting for %1 to finish booting…").arg(serial));
            }
            scheduleNext();    // watchdog: keep polling until mirror is live or timeout
        });
    };

    // Step 1: WiFi devices lose their adb TCP session on reboot — re-establish it
    // first (result ignored), then check the boot state.
    if (serial.contains(':')) {
        runAdbAsync(adb, {"connect", serial}, 5000,
                    [checkBoot](const QString &) { checkBoot(); });
    } else {
        checkBoot();
    }
}

void FarmWindow::setNumberedWallpapers()
{
    if (m_selectedSerials.isEmpty()) {
        m_statusBar->setText(tr("No devices selected."));
        return;
    }

    m_statusBar->setText(tr("Setting numbered wallpapers..."));

    // Create a temporary directory for wallpapers
    const QString tempDir = QCoreApplication::applicationDirPath() + "/temp_wallpapers";
    QDir().mkpath(tempDir);

    for (const QString &serial : m_selectedSerials) {
        if (!m_numbering.contains(serial)) {
            continue;
        }

        const int deviceNumber = m_numbering.value(serial);

        // Generate wallpaper image (1080x1920)
        QImage wallpaper(1080, 1920, QImage::Format_ARGB32);
        wallpaper.fill(Qt::black);  // Default black background

        // Load and render the SVG template with LunaSVG
        const QString svgPath = QCoreApplication::applicationDirPath() + "/wallpaper_template.svg";
        auto document = lunasvg::Document::loadFromFile(svgPath.toStdString());

        if (document) {
            // Render SVG to bitmap at 1080x1920
            auto bitmap = document->renderToBitmap(1080, 1920);

            if (bitmap.valid()) {
                // Convert LunaSVG bitmap to QImage
                // LunaSVG uses RGBA format
                QImage svgImage(bitmap.data(), static_cast<int>(bitmap.width()),
                               static_cast<int>(bitmap.height()),
                               static_cast<int>(bitmap.stride()),
                               QImage::Format_RGBA8888);

                // Copy SVG image to wallpaper
                QPainter basePainter(&wallpaper);
                basePainter.drawImage(0, 0, svgImage);
                basePainter.end();
            }
        }

        // Draw number at the top (above the phone illustration)
        QPainter painter(&wallpaper);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(Qt::white);
        QFont numberFont("Arial", 200, QFont::Black);
        painter.setFont(numberFont);

        // Position the number in the upper portion (Y: 350-550)
        QRect numberRect(0, 350, 1080, 200);
        painter.drawText(numberRect, Qt::AlignCenter, QString::number(deviceNumber));
        painter.end();

        // Save wallpaper
        const QString wallpaperPath = tempDir + QString("/wallpaper_%1.png").arg(deviceNumber);
        wallpaper.save(wallpaperPath, "PNG");

        // Use dd to pipe the image directly to the wallpaper setter
        // This works on all Android versions without needing storage permissions
        QProcess *setProcess = new QProcess(this);

        connect(setProcess, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
                this, [this, serial, deviceNumber, setProcess](int exitCode, QProcess::ExitStatus status) {
            if (status == QProcess::NormalExit && exitCode == 0) {
                qDebug() << "Wallpaper set successfully for device" << deviceNumber << "serial:" << serial;
            } else {
                const QString error = QString::fromUtf8(setProcess->readAllStandardError());
                qDebug() << "Failed to set wallpaper for device" << deviceNumber << "error:" << error;
            }
            setProcess->deleteLater();
        });

        // Method: push file then use WallpaperManager via shell
        const QString remotePath = QString("/sdcard/gf_wp_%1.png").arg(deviceNumber);

        QProcess *pushProcess = new QProcess(this);
        connect(pushProcess, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
                this, [this, serial, remotePath, setProcess, pushProcess](int exitCode, QProcess::ExitStatus status) {
            pushProcess->deleteLater();

            if (status != QProcess::NormalExit || exitCode != 0) {
                qDebug() << "Failed to push wallpaper to" << serial;
                setProcess->deleteLater();
                return;
            }

            // Copy to app's accessible directory then use helper
            const QString appPath = "/sdcard/Android/data/com.farmer333.wallpaperhelper/files/wallpaper.png";

            QProcess *copyProcess = new QProcess(this);
            connect(copyProcess, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
                    this, [this, serial, appPath, setProcess, copyProcess](int exitCode, QProcess::ExitStatus status) {
                copyProcess->deleteLater();

                // Try to use helper APK
                setProcess->setProgram("adb");
                setProcess->setArguments(QStringList()
                    << "-s" << serial
                    << "shell"
                    << "am" << "start"
                    << "-n" << "com.farmer333.wallpaperhelper/.SetWallpaperActivity"
                    << "-e" << "file" << appPath);
                setProcess->start();
            });

            // Create directory and copy file
            copyProcess->setProgram("adb");
            copyProcess->setArguments(QStringList()
                << "-s" << serial
                << "shell"
                << QString("mkdir -p /sdcard/Android/data/com.farmer333.wallpaperhelper/files && cp %1 %2")
                    .arg(remotePath, appPath));
            copyProcess->start();
        });

        pushProcess->setProgram("adb");
        pushProcess->setArguments(QStringList()
            << "-s" << serial
            << "push"
            << wallpaperPath
            << remotePath);
        pushProcess->start();
    }

    m_statusBar->setText(tr("Wallpapers set for %1 devices").arg(m_selectedSerials.size()));
}

void FarmWindow::checkAndInstallHelperApk(const QString &serial)
{
    // Check if the helper APK is already installed
    QProcess *checkProcess = new QProcess(this);

    connect(checkProcess, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [this, serial, checkProcess](int exitCode, QProcess::ExitStatus status) {
        checkProcess->deleteLater();

        if (status != QProcess::NormalExit) {
            return;
        }

        const QString output = QString::fromUtf8(checkProcess->readAllStandardOutput());

        // If package is found, output contains "package:com.farmer333.wallpaperhelper"
        if (output.contains("com.farmer333.wallpaperhelper")) {
            qDebug() << "333Farmer Helper APK already installed on" << serial;
            return;
        }

        // APK not found, install it
        qDebug() << "Installing 333Farmer Helper APK on" << serial;
        m_statusBar->setText(tr("Installing 333Farmer Helper on %1...").arg(serial));

        const QString apkPath = QCoreApplication::applicationDirPath() + "/333FarmerWallpaperHelper.apk";

        QProcess *installProcess = new QProcess(this);
        connect(installProcess, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
                this, [this, serial, installProcess](int exitCode, QProcess::ExitStatus status) {
            installProcess->deleteLater();

            if (status == QProcess::NormalExit && exitCode == 0) {
                qDebug() << "333Farmer Helper APK installed successfully on" << serial;
                m_statusBar->setText(tr("333Farmer Helper installed on %1").arg(serial));
            } else {
                const QString error = QString::fromUtf8(installProcess->readAllStandardError());
                qDebug() << "Failed to install 333Farmer Helper APK on" << serial << "error:" << error;
            }
        });

        installProcess->setProgram("adb");
        installProcess->setArguments(QStringList()
            << "-s" << serial
            << "install"
            << "-r"  // replace existing
            << apkPath);
        installProcess->start();
    });

    checkProcess->setProgram("adb");
    checkProcess->setArguments(QStringList()
        << "-s" << serial
        << "shell"
        << "pm" << "list" << "packages"
        << "com.farmer333.wallpaperhelper");
    checkProcess->start();
}

void FarmWindow::keepScreenOnSelected()
{
    if (m_selectedSerials.isEmpty()) {
        m_statusBar->setText(tr("No devices selected."));
        return;
    }

    m_statusBar->setText(tr("Setting screen timeout to never on %1 device(s)...").arg(m_selectedSerials.size()));

    for (const QString &serial : m_selectedSerials) {
        QProcess *process = new QProcess(this);

        connect(process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
                this, [this, serial, process](int exitCode, QProcess::ExitStatus status) {
            process->deleteLater();

            if (status == QProcess::NormalExit && exitCode == 0) {
                qDebug() << "Screen timeout disabled on" << serial;
            } else {
                const QString error = QString::fromUtf8(process->readAllStandardError());
                qDebug() << "Failed to disable screen timeout on" << serial << "error:" << error;
            }
        });

        // Set screen timeout to maximum (2147483647 milliseconds = ~24 days, effectively never)
        process->setProgram("adb");
        process->setArguments(QStringList()
            << "-s" << serial
            << "shell"
            << "settings" << "put" << "system" << "screen_off_timeout" << "2147483647");
        process->start();
    }

    m_statusBar->setText(tr("Screen timeout set to never on %1 device(s)").arg(m_selectedSerials.size()));
}

void FarmWindow::restoreScreenTimeoutSelected()
{
    if (m_selectedSerials.isEmpty()) {
        m_statusBar->setText(tr("No devices selected."));
        return;
    }

    m_statusBar->setText(tr("Restoring screen timeout on %1 device(s)...").arg(m_selectedSerials.size()));

    for (const QString &serial : m_selectedSerials) {
        QProcess *process = new QProcess(this);

        connect(process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
                this, [this, serial, process](int exitCode, QProcess::ExitStatus status) {
            process->deleteLater();

            if (status == QProcess::NormalExit && exitCode == 0) {
                qDebug() << "Screen timeout restored on" << serial;
            } else {
                const QString error = QString::fromUtf8(process->readAllStandardError());
                qDebug() << "Failed to restore screen timeout on" << serial << "error:" << error;
            }
        });

        // Restore default screen timeout (30 seconds = 30000 milliseconds)
        process->setProgram("adb");
        process->setArguments(QStringList()
            << "-s" << serial
            << "shell"
            << "settings" << "put" << "system" << "screen_off_timeout" << "30000");
        process->start();
    }

    m_statusBar->setText(tr("Screen timeout restored on %1 device(s)").arg(m_selectedSerials.size()));
}
