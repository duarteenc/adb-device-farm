#include "farmwindow.h"

#include <algorithm>
#include <cmath>
#include <utility>

#include <QCheckBox>
#include <QCoreApplication>
#include <QEvent>
#include <QFileInfo>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QProcess>
#include <QPushButton>
#include <QRandomGenerator>
#include <QResizeEvent>
#include <QRubberBand>
#include <QScrollArea>
#include <QSettings>
#include <QSlider>
#include <QVBoxLayout>
#include <QWheelEvent>

#include "config.h"
#include "devicetile.h"
#include "focuspanel.h"

namespace {
constexpr int kGridSpacing = 10;
constexpr int kGridMargin = 12;
constexpr int kMinTileWidth = 160;    // floor: phones unreadable below this
constexpr int kMaxConcurrent = 8;     // simultaneous connection setups
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
#tileIp { background: transparent; color:#dbe4f0; font-size:10px; }
#tileFps { background: transparent; color:#dbe4f0; font-size:9px; }
)";
}

FarmWindow::FarmWindow(QWidget *parent)
    : QWidget(parent)
{
    setWindowTitle(tr("ADB Device Farm — v2.0"));
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

    auto *title = new QLabel(tr("Control Center"), panel);
    title->setObjectName("panelTitle");

    auto *refreshBtn = new QPushButton(tr("Refresh"), panel);
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

    // WiFi
    m_ipEdit = new QLineEdit(panel);
    m_ipEdit->setPlaceholderText(tr("192.168.1.50:5555"));
    auto *connectBtn = new QPushButton(tr("Connect"), panel);
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
    col->addWidget(title);
    col->addWidget(refreshBtn);
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
    col->addWidget(sep());
    col->addWidget(buildSelectorSection());
    col->addWidget(sep());
    col->addWidget(new QLabel(tr("WiFi connect"), panel));
    col->addWidget(m_ipEdit);
    col->addWidget(connectBtn);
    col->addWidget(enableWifiBtn);
    col->addStretch(1);
    col->addWidget(m_statusBar);

    connect(refreshBtn, &QPushButton::clicked, this, &FarmWindow::refreshDevices);
    connect(mirrorAllBtn, &QPushButton::clicked, this, &FarmWindow::mirrorAll);
    connect(stopAllBtn, &QPushButton::clicked, this, &FarmWindow::stopAll);
    connect(connectBtn, &QPushButton::clicked, this, &FarmWindow::connectWifi);
    connect(m_ipEdit, &QLineEdit::returnPressed, this, &FarmWindow::connectWifi);
    connect(enableWifiBtn, &QPushButton::clicked, this, &FarmWindow::enableWifiSelected);
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

    auto *gridHost = new QWidget(w);
    gridHost->setObjectName("selectorGridHost");
    m_selectorGrid = new QGridLayout(gridHost);
    m_selectorGrid->setContentsMargins(0, 0, 0, 0);
    m_selectorGrid->setSpacing(4);
    m_selectorGrid->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    auto *gridScroll = new QScrollArea(w);
    gridScroll->setObjectName("selectorScroll");
    gridScroll->setWidgetResizable(true);
    gridScroll->setFixedHeight(150);
    gridScroll->setFrameShape(QFrame::NoFrame);
    gridScroll->setWidget(gridHost);
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
            auto *tile = new DeviceTile(serial);
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

            m_tiles.insert(serial, tile);
        }
    }

    // Rebuild m_order from scratch using ALL sorted devices
    m_order = sorted;
    relayout();
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
        const QString &s = it.key();
        QPushButton *b = it.value();
        const bool selected = m_selectedSerials.contains(s);
        const bool mirroring = m_tiles.contains(s);
        const QString bg = selected ? QStringLiteral("#2563eb") : QStringLiteral("#1c2436");
        const QString border = mirroring ? QStringLiteral("#22c55e") : QStringLiteral("#2a344a");
        b->setChecked(selected);
        b->setStyleSheet(QStringLiteral("QPushButton{background:%1;border:1px solid %2;"
                                        "border-radius:4px;color:#e2e8f0;font-size:11px;}")
                             .arg(bg, border));
    }
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
    updateHostTargets();    // keep the host's broadcast set in sync with the selection
}

QString FarmWindow::tileAt(const QPoint &point) const
{
    for (auto it = m_tiles.begin(); it != m_tiles.end(); ++it) {
        if (it.value()->geometry().contains(point)) {
            return it.key();
        }
    }
    return QString();
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
        if (it.value()->geometry().intersects(rect)) {
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
    if (m_selectedSerials.isEmpty()) {
        m_statusBar->setText(tr("Select devices first to make a group."));
        return;
    }
    bool ok = false;
    const QString name =
        QInputDialog::getText(this, tr("New group"), tr("Group name:"), QLineEdit::Normal,
                              QString(), &ok)
            .trimmed();
    if (!ok || name.isEmpty()) {
        return;
    }
    QStringList serials;
    for (const QString &s : m_selectedSerials) {
        serials << s;
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
    QStringList names = m_groups.keys();
    names.sort();
    for (const QString &name : names) {
        auto *rowW = new QWidget;
        auto *row = new QHBoxLayout(rowW);
        row->setContentsMargins(0, 0, 0, 0);
        row->setSpacing(4);
        auto *applyBtn =
            new QPushButton(QStringLiteral("%1 (%2)").arg(name).arg(m_groups[name].size()), rowW);
        auto *delBtn = new QPushButton(tr("x"), rowW);
        delBtn->setFixedWidth(24);
        row->addWidget(applyBtn, 1);
        row->addWidget(delBtn, 0);
        m_groupsLayout->addWidget(rowW);
        connect(applyBtn, &QPushButton::clicked, this, [this, name] { applyGroup(name); });
        connect(delBtn, &QPushButton::clicked, this, [this, name] {
            m_groups.remove(name);
            saveGroups();
            rebuildGroups();
        });
    }
}

void FarmWindow::loadGroups()
{
    QSettings settings(QStringLiteral("ZamiApp"), QStringLiteral("AdbDeviceFarm"));
    settings.beginGroup(QStringLiteral("farm_groups"));
    const QStringList names = settings.childKeys();
    for (const QString &name : names) {
        m_groups.insert(name, settings.value(name).toStringList());
    }
    settings.endGroup();
}

void FarmWindow::saveGroups()
{
    QSettings settings(QStringLiteral("ZamiApp"), QStringLiteral("AdbDeviceFarm"));
    settings.beginGroup(QStringLiteral("farm_groups"));
    settings.remove(QString());
    for (auto it = m_groups.begin(); it != m_groups.end(); ++it) {
        settings.setValue(it.key(), it.value());
    }
    settings.endGroup();
}

void FarmWindow::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    relayout();
}

bool FarmWindow::eventFilter(QObject *watched, QEvent *event)
{
    // The grid's available width changes when the host panel shows/hides or the
    // window resizes; reflow the columns whenever the viewport is resized.
    if (m_scroll && watched == m_scroll->viewport() && event->type() == QEvent::Resize) {
        relayout();
        return QWidget::eventFilter(watched, event);
    }

    // Marquee selection on the grid background (tiles are mouse-transparent).
    if (watched == m_gridHost) {
        switch (event->type()) {
        case QEvent::MouseButtonPress: {
            auto *me = static_cast<QMouseEvent *>(event);
            if (me->button() == Qt::LeftButton) {
                m_rubberOrigin = me->position().toPoint();
                m_dragging = false;
            }
            break;
        }
        case QEvent::MouseMove: {
            auto *me = static_cast<QMouseEvent *>(event);
            if (me->buttons() & Qt::LeftButton) {
                const QPoint p = me->position().toPoint();
                if (!m_dragging && (p - m_rubberOrigin).manhattanLength() > 6) {
                    m_dragging = true;
                    m_rubberBand->setGeometry(QRect(m_rubberOrigin, QSize()));
                    m_rubberBand->show();
                    m_rubberBand->raise();
                }
                if (m_dragging) {
                    m_rubberBand->setGeometry(QRect(m_rubberOrigin, p).normalized());
                }
            }
            break;
        }
        case QEvent::MouseButtonRelease: {
            auto *me = static_cast<QMouseEvent *>(event);
            if (me->button() != Qt::LeftButton) {
                break;
            }
            if (m_dragging) {
                const QRect r = m_rubberBand->geometry();
                m_rubberBand->hide();
                m_dragging = false;
                applyRubberSelection(r, me->modifiers().testFlag(Qt::ControlModifier));
            } else {
                const QString serial = tileAt(me->position().toPoint());
                if (!serial.isEmpty()) {
                    onTileClicked(serial);    // toggle this tile's selection
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

void FarmWindow::connectWifi()
{
    QString target = m_ipEdit->text().trimmed();
    if (target.isEmpty()) {
        m_statusBar->setText(tr("Enter an ip:port to connect."));
        return;
    }
    if (!target.contains(':')) {
        target += ":5555";
    }
    m_adb.execute("", QStringList() << "connect" << target);
    m_statusBar->setText(tr("connecting to %1…").arg(target));
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
    ensureTile(serial)->setStatusText(tr("connecting…"));

    // Normalize resolution/density BEFORE scrcpy captures, so every phone streams
    // and accepts control at the same coordinate space (mixed native resolutions
    // otherwise make broadcast input land in the wrong place on some models).
    const QString adb = Config::getInstance().getAdbPath();
    QProcess::execute(adb, {"-s", serial, "shell",
                            QStringLiteral("wm size %1 ; wm density %2")
                                .arg(QLatin1String(kNormalizedSize), QLatin1String(kNormalizedDensity))});

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
        removeTile(serial);
        return false;
    }
    return true;
}

void FarmWindow::onDeviceConnected(bool success, const QString &serial, const QString &deviceName,
                                   const QSize &size)
{
    Q_UNUSED(size);
    m_connecting.remove(serial);
    if (!success) {
        removeTile(serial);
        pumpConnectQueue();    // free the slot; start the next queued device
        return;
    }

    DeviceTile *tile = ensureTile(serial);
    tile->setModel(deviceName);
    tile->setStatusText(tr("connected"));

    auto device = qsc::IDeviceManage::getInstance().getDevice(serial);
    if (device) {
        device->setUserData(static_cast<void *>(tile));
        device->registerDeviceObserver(tile);
    }

    // Re-sort tiles after connection to maintain numerical order
    rebuildNumbering();

    updateSelectorStyles();    // mark this device as mirroring in the selector
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
    removeTile(serial);
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
    auto *tile = new DeviceTile(serial);
    tile->setTileWidth(m_tileWidth);
    tile->setControllable(m_smallViewControl);
    // Per-tile control (active only when the tile is controllable; otherwise the
    // tile is mouse-transparent and the grid drives marquee selection instead).
    connect(tile, &DeviceTile::mouseInput, this, &FarmWindow::onTileMouse);
    connect(tile, &DeviceTile::wheelInput, this, &FarmWindow::onTileWheel);
    connect(tile, &DeviceTile::keyInput, this, &FarmWindow::onTileKey);
    connect(tile, &DeviceTile::doubleClicked, this, &FarmWindow::onTileDoubleClicked);
    m_tiles.insert(serial, tile);
    m_order.append(serial);
    relayout();
    return tile;
}

void FarmWindow::removeTile(const QString &serial)
{
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

    const int count = static_cast<int>(m_order.size());
    if (count == 0) {
        return;
    }

    const int avail = m_scroll->viewport()->width() - 2 * kGridMargin;
    const int per = m_tileWidth + kGridSpacing;
    int cols = std::max(1, (avail + kGridSpacing) / per);
    cols = std::min(cols, count);

    for (int i = 0; i < count; ++i) {
        DeviceTile *tile = m_tiles.value(m_order.at(i), nullptr);
        if (!tile) {
            continue;
        }
        tile->setNumber(m_numbering.value(m_order.at(i), i + 1));
        m_grid->addWidget(tile, i / cols, i % cols, Qt::AlignTop | Qt::AlignLeft);
    }
    const int rows = (count + cols - 1) / cols;
    m_grid->setColumnStretch(cols, 1);    // push tiles to the left
    m_grid->setRowStretch(rows, 1);       // push tiles to the top
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
