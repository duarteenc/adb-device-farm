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
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPushButton>
#include <QRandomGenerator>
#include <QResizeEvent>
#include <QScrollArea>
#include <QSlider>
#include <QVBoxLayout>
#include <QWheelEvent>

#include "devicetile.h"

namespace {
constexpr int kGridSpacing = 10;
constexpr int kGridMargin = 12;
constexpr int kMinTileWidth = 160;    // floor: phones unreadable below this
constexpr int kMaxConcurrent = 8;     // simultaneous connection setups
constexpr quint16 kBasePort = 27183;  // reverse-tunnel port base (unique per device)

const char *kStyle = R"(
QWidget { background:#0b0f17; color:#e2e8f0; font-size:12px; }
#controlPanel { background:#121826; border-right:1px solid #1e2636; }
#panelTitle { font-size:16px; font-weight:bold; padding:2px 0 8px 0; }
QPushButton { background:#1c2436; border:1px solid #2a344a; border-radius:5px; padding:6px 10px; }
QPushButton:hover { background:#26314a; }
QPushButton#primary { background:#2563eb; border:none; }
QPushButton#primary:hover { background:#3b82f6; }
QLineEdit { background:#0b0f17; border:1px solid #2a344a; border-radius:4px; padding:4px; }
QCheckBox { padding:2px; }
QLabel#hint { color:#7c8aa0; }
QScrollArea, #gridHost { border:none; background:#0b0f17; }
QSlider::groove:horizontal { height:4px; background:#2a344a; border-radius:2px; }
QSlider::handle:horizontal { width:14px; margin:-6px 0; border-radius:7px; background:#3b82f6; }
QSlider::sub-page:horizontal { background:#2563eb; border-radius:2px; }
#tileHeader { background:#0b0f17; }
#tileNum { color:#e2e8f0; font-size:15px; font-weight:bold; }
#tileModel { color:#cbd5e1; font-size:11px; }
#tileIp { color:#7c8aa0; font-size:10px; }
#tileFps { color:#7c8aa0; font-size:10px; }
)";
}

FarmWindow::FarmWindow(QWidget *parent)
    : QWidget(parent)
{
    setWindowTitle(tr("ADB Device Farm — v2.0"));
    setStyleSheet(QString::fromUtf8(kStyle));

    // --- Scrollable grid (right) ---
    auto *gridHost = new QWidget;
    gridHost->setObjectName("gridHost");
    m_grid = new QGridLayout(gridHost);
    m_grid->setContentsMargins(kGridMargin, kGridMargin, kGridMargin, kGridMargin);
    m_grid->setSpacing(kGridSpacing);

    m_scroll = new QScrollArea(this);
    m_scroll->setWidgetResizable(true);
    m_scroll->setWidget(gridHost);

    auto *root = new QHBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);
    root->addWidget(buildControlPanel(), 0);
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
    panel->setFixedWidth(240);

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
    auto qualityRow = makeSlider(tr("Quality (px)"), 320, 1280, static_cast<int>(m_maxSize), &m_qualityValue);
    auto fpsRow = makeSlider(tr("Frame rate"), 15, 60, static_cast<int>(m_maxFps), &m_fpsValue);

    connect(tileRow.second, &QSlider::valueChanged, this, &FarmWindow::setTileSize);
    connect(qualityRow.second, &QSlider::valueChanged, this, &FarmWindow::setQuality);
    connect(fpsRow.second, &QSlider::valueChanged, this, &FarmWindow::setFrameRate);

    auto *groupCheck = new QCheckBox(tr("Control All (group input)"), panel);

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
    col->addLayout(qualityRow.first);
    col->addLayout(fpsRow.first);
    col->addWidget(new QLabel(tr("Quality/FPS apply on next Mirror All."), panel));
    col->itemAt(col->count() - 1)->widget()->setObjectName("hint");
    col->addWidget(sep());
    col->addWidget(groupCheck);
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

    return panel;
}

void FarmWindow::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    relayout();
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
    // Queue every not-yet-connected device; the pump drains the queue with a
    // bounded number of simultaneous setups.
    for (const QString &serial : m_available) {
        if (!m_tiles.contains(serial) && !m_connecting.contains(serial)
            && !m_pending.contains(serial)) {
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
        if (m_tiles.contains(serial) || m_connecting.contains(serial)) {
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
    pumpConnectQueue();    // a slot just freed up; start the next queued device
}

void FarmWindow::onDeviceDisconnected(const QString &serial)
{
    auto device = qsc::IDeviceManage::getInstance().getDevice(serial);
    DeviceTile *tile = m_tiles.value(serial, nullptr);
    if (device && tile) {
        device->deRegisterDeviceObserver(tile);
    }
    m_connecting.remove(serial);
    removeTile(serial);
    pumpConnectQueue();
}

void FarmWindow::onTileClicked(const QString &serial)
{
    if (!m_selected.isEmpty() && m_tiles.contains(m_selected)) {
        m_tiles[m_selected]->setSelected(false);
    }
    m_selected = serial;
    if (m_tiles.contains(serial)) {
        m_tiles[serial]->setSelected(true);
    }
}

QList<QString> FarmWindow::inputTargets(const QString &sourceSerial) const
{
    if (m_groupMode) {
        return m_order;
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
    m_statusBar->setText(on ? tr("Control All ON — input goes to every device.")
                            : tr("Control All OFF — input goes to the focused device."));
}

DeviceTile *FarmWindow::ensureTile(const QString &serial)
{
    auto it = m_tiles.find(serial);
    if (it != m_tiles.end()) {
        return it.value();
    }
    auto *tile = new DeviceTile(serial);
    tile->setTileWidth(m_tileWidth);
    connect(tile, &DeviceTile::clicked, this, &FarmWindow::onTileClicked);
    connect(tile, &DeviceTile::mouseInput, this, &FarmWindow::onTileMouse);
    connect(tile, &DeviceTile::wheelInput, this, &FarmWindow::onTileWheel);
    connect(tile, &DeviceTile::keyInput, this, &FarmWindow::onTileKey);
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
        tile->setNumber(i + 1);
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
