#include "devicesdialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

namespace {
const char *kStyle = R"(
QDialog {
    background:#0b0f17;
    color:#e2e8f0;
    font-size:13px;
}
QLabel {
    background:transparent;
    color:#e2e8f0;
}
#titleLabel {
    font-size:18px;
    font-weight:bold;
    color:#ffffff;
    padding:4px 0;
}
#totalLabel {
    font-size:13px;
    color:#e2e8f0;
    font-weight:bold;
}
QPushButton {
    background:transparent;
    border:1px solid #4169e1;
    border-radius:5px;
    padding:6px 16px;
    color:#4169e1;
    font-size:12px;
    min-height:24px;
}
QPushButton:hover {
    background:rgba(65,105,225,0.1);
    border-color:#5a7df5;
}
QPushButton#restart {
    border-color:#dc2626;
    color:#dc2626;
}
QPushButton#restart:hover {
    background:rgba(220,38,38,0.1);
    border-color:#ef4444;
}
QPushButton#primary {
    background:#4169e1;
    border:none;
    color:#ffffff;
}
QPushButton#primary:hover {
    background:#5a7df5;
}
QLineEdit, QComboBox {
    background:#1a1f2e;
    border:1px solid #2a344a;
    border-radius:4px;
    padding:6px 10px;
    color:#e2e8f0;
    font-size:12px;
}
QLineEdit:focus, QComboBox:focus {
    border-color:#4169e1;
}
QComboBox::drop-down {
    subcontrol-origin:padding;
    subcontrol-position:center right;
    border:none;
    width:24px;
}
QComboBox::down-arrow {
    image:url(:/image/combo/arrow-down.png);
    width:12px;
    height:8px;
}
QComboBox QAbstractItemView {
    background:#1a1f2e;
    border:1px solid #2a344a;
    selection-background-color:#4169e1;
    color:#e2e8f0;
}
QTableWidget {
    background:#0b0f17;
    border:1px solid #2a344a;
    border-radius:6px;
    gridline-color:#1e2636;
    color:#e2e8f0;
    font-size:13px;
}
QTableWidget::item {
    padding:8px;
    border-bottom:1px solid #1e2636;
}
QTableWidget::item:selected {
    background:#1e3a5f;
}
QHeaderView::section {
    background:#121826;
    color:#94a3b8;
    border:none;
    border-bottom:1px solid #2a344a;
    padding:10px 8px;
    font-weight:600;
    font-size:12px;
}
QCheckBox {
    background:transparent;
    color:#e2e8f0;
    spacing:6px;
}
QCheckBox::indicator {
    width:16px;
    height:16px;
    border:1px solid #2a344a;
    border-radius:3px;
    background:#0b0f17;
}
QCheckBox::indicator:checked {
    background:#4169e1;
    border-color:#4169e1;
}
QCheckBox::indicator:checked::after {
    content:'✓';
    color:#ffffff;
}
QScrollBar:vertical {
    background:#0b0f17;
    width:10px;
    border:none;
    margin:0px;
}
QScrollBar::handle:vertical {
    background:#2a344a;
    border-radius:5px;
    min-height:20px;
}
QScrollBar::handle:vertical:hover {
    background:#3b4a5a;
}
QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical {
    border:none;
    background:none;
    height:0px;
}
QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical {
    background:none;
}
QMenu {
    background:#1a1f2e;
    border:1px solid #2a344a;
    padding:4px;
    color:#e2e8f0;
}
QMenu::item {
    padding:6px 24px 6px 12px;
    border-radius:3px;
}
QMenu::item:selected {
    background:#4169e1;
}
)";

constexpr int kIconColumn = 0;
constexpr int kIndexColumn = 1;
constexpr int kModelColumn = 2;
constexpr int kSerialColumn = 3;
constexpr int kActionColumn = 4;
constexpr int kMenuColumn = 5;
}

DevicesDialog::DevicesDialog(QWidget *parent)
    : QDialog(parent)
{
    setupUi();
    setStyleSheet(QString::fromUtf8(kStyle));
}

DevicesDialog::~DevicesDialog() = default;

void DevicesDialog::setupUi()
{
    setWindowTitle(tr("Dispositivos"));
    resize(1520, 800);

    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setSpacing(12);
    mainLayout->setContentsMargins(20, 20, 20, 20);

    // --- Header row: title + close button ---
    auto *headerLayout = new QHBoxLayout;
    auto *titleIcon = new QLabel("⁙");
    titleIcon->setObjectName("titleLabel");
    titleIcon->setFixedWidth(20);
    auto *titleLabel = new QLabel(tr("Dispositivos"));
    titleLabel->setObjectName("titleLabel");
    headerLayout->addWidget(titleIcon);
    headerLayout->addWidget(titleLabel);
    headerLayout->addStretch();

    auto *closeButton = new QPushButton("✕");
    closeButton->setFixedSize(28, 28);
    closeButton->setStyleSheet("QPushButton { border:none; color:#7c8aa0; font-size:18px; padding:0; } "
                                "QPushButton:hover { color:#e2e8f0; background:#1e2636; }");
    connect(closeButton, &QPushButton::clicked, this, &QDialog::reject);
    headerLayout->addWidget(closeButton);
    mainLayout->addLayout(headerLayout);

    // --- Top controls row: total, Recargar, Reiniciar ADB ---
    auto *topRow = new QHBoxLayout;
    topRow->setSpacing(12);

    m_totalLabel = new QLabel(tr("Total: 0"));
    m_totalLabel->setObjectName("totalLabel");
    topRow->addWidget(m_totalLabel);

    topRow->addStretch();

    m_refreshButton = new QPushButton(tr("Recargar"));
    m_refreshButton->setObjectName("primary");
    connect(m_refreshButton, &QPushButton::clicked, this, &DevicesDialog::onRefresh);
    topRow->addWidget(m_refreshButton);

    m_restartAdbButton = new QPushButton(tr("⏻ Reiniciar ADB"));
    m_restartAdbButton->setObjectName("restart");
    connect(m_restartAdbButton, &QPushButton::clicked, this, &DevicesDialog::onRestartAdb);
    topRow->addWidget(m_restartAdbButton);

    mainLayout->addLayout(topRow);

    // --- Filter row: Search + Groups ---
    auto *filterRow = new QHBoxLayout;
    filterRow->setSpacing(12);

    m_searchEdit = new QLineEdit;
    m_searchEdit->setPlaceholderText(tr("Buscar"));
    m_searchEdit->setFixedWidth(280);
    connect(m_searchEdit, &QLineEdit::textChanged, this, &DevicesDialog::onSearchChanged);
    filterRow->addWidget(m_searchEdit);

    m_groupsCombo = new QComboBox;
    m_groupsCombo->addItem(tr("Grupos"));
    m_groupsCombo->setFixedWidth(180);
    connect(m_groupsCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &DevicesDialog::onGroupChanged);
    filterRow->addWidget(m_groupsCombo);

    filterRow->addStretch();
    mainLayout->addLayout(filterRow);

    // --- Table ---
    m_table = new QTableWidget;
    m_table->setColumnCount(6);
    m_table->setHorizontalHeaderLabels({"", tr("Index"), tr("Phone name"), tr("Device ID"), "", ""});
    m_table->horizontalHeader()->setStretchLastSection(false);
    m_table->horizontalHeader()->setSectionResizeMode(kIconColumn, QHeaderView::Fixed);
    m_table->horizontalHeader()->setSectionResizeMode(kIndexColumn, QHeaderView::Fixed);
    m_table->horizontalHeader()->setSectionResizeMode(kModelColumn, QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(kSerialColumn, QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(kActionColumn, QHeaderView::Fixed);
    m_table->horizontalHeader()->setSectionResizeMode(kMenuColumn, QHeaderView::Fixed);
    m_table->setColumnWidth(kIconColumn, 50);
    m_table->setColumnWidth(kIndexColumn, 80);
    m_table->setColumnWidth(kActionColumn, 140);
    m_table->setColumnWidth(kMenuColumn, 50);
    m_table->verticalHeader()->setVisible(false);
    m_table->setSelectionBehavior(QTableWidget::SelectRows);
    m_table->setSelectionMode(QTableWidget::NoSelection);
    m_table->setEditTriggers(QTableWidget::NoEditTriggers);
    m_table->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_table, &QTableWidget::customContextMenuRequested,
            this, &DevicesDialog::showDeviceContextMenu);

    mainLayout->addWidget(m_table);

    // --- Pagination row ---
    auto *paginationRow = new QHBoxLayout;
    paginationRow->setSpacing(8);
    paginationRow->addStretch();

    // Own stylesheet so the global QPushButton padding doesn't squash the arrows.
    const char *kPageBtnStyle =
        "QPushButton { background:#1a1f2e; border:1px solid #2a344a; border-radius:4px; "
        "color:#e2e8f0; font-size:16px; padding:0; } "
        "QPushButton:hover:enabled { background:#26314a; border-color:#4169e1; } "
        "QPushButton:disabled { color:#475569; border-color:#1e2636; }";

    m_prevPageButton = new QPushButton("‹");
    m_prevPageButton->setFixedSize(32, 32);
    m_prevPageButton->setStyleSheet(kPageBtnStyle);
    connect(m_prevPageButton, &QPushButton::clicked, [this]() {
        if (m_currentPage > 1) {
            onPageChanged(m_currentPage - 1);
        }
    });
    paginationRow->addWidget(m_prevPageButton);

    m_paginationLabel = new QLabel("1");
    m_paginationLabel->setAlignment(Qt::AlignCenter);
    m_paginationLabel->setFixedSize(32, 32);
    m_paginationLabel->setStyleSheet("QLabel { background:#4169e1; border-radius:4px; "
                                     "color:#ffffff; font-weight:bold; }");
    paginationRow->addWidget(m_paginationLabel);

    m_nextPageButton = new QPushButton("›");
    m_nextPageButton->setFixedSize(32, 32);
    m_nextPageButton->setStyleSheet(kPageBtnStyle);
    connect(m_nextPageButton, &QPushButton::clicked, [this]() {
        int totalPages = (filteredDevices().size() + m_perPage - 1) / m_perPage;
        if (m_currentPage < totalPages) {
            onPageChanged(m_currentPage + 1);
        }
    });
    paginationRow->addWidget(m_nextPageButton);

    paginationRow->addSpacing(12);

    m_perPageCombo = new QComboBox;
    m_perPageCombo->addItems({tr("10 / page"), tr("25 / page"),
                              tr("50 / page"), tr("100 / page")});
    m_perPageCombo->setFixedSize(110, 32);
    connect(m_perPageCombo, &QComboBox::currentTextChanged,
            this, &DevicesDialog::onPerPageChanged);
    paginationRow->addWidget(m_perPageCombo);

    mainLayout->addLayout(paginationRow);
}

void DevicesDialog::setDevices(const QList<DeviceInfo> &devices)
{
    m_devices = devices;
    rebuildTable();
    updateTotalLabel();
}

QStringList DevicesDialog::selectedSerials() const
{
    return m_selectedSerials.values();
}

void DevicesDialog::onRefresh()
{
    emit refreshRequested();
}

void DevicesDialog::onRestartAdb()
{
    emit restartAdbRequested();
}

void DevicesDialog::onRestartDevice(const QString &serial)
{
    emit restartDeviceRequested(serial);
}

void DevicesDialog::onSearchChanged(const QString &text)
{
    m_searchText = text.trimmed();
    m_currentPage = 1;
    rebuildTable();
}

void DevicesDialog::onGroupChanged(int index)
{
    m_currentGroup = (index > 0) ? m_groupsCombo->itemText(index) : QString();
    m_currentPage = 1;
    rebuildTable();
}

void DevicesDialog::onSelectAllToggled(bool checked)
{
    if (checked) {
        for (const auto &dev : filteredDevices()) {
            m_selectedSerials.insert(dev.serial);
        }
    } else {
        m_selectedSerials.clear();
    }
    rebuildTable();
}

void DevicesDialog::onRowSelectionChanged()
{
    rebuildTable();
}

void DevicesDialog::onPageChanged(int page)
{
    m_currentPage = page;
    rebuildTable();
    updatePagination();
}

void DevicesDialog::onPerPageChanged(const QString &text)
{
    // Items look like "10 / page" — pull the leading number out.
    bool ok = false;
    int val = text.section('/', 0, 0).trimmed().toInt(&ok);
    if (ok && val > 0) {
        m_perPage = val;
        m_currentPage = 1;
        rebuildTable();
    }
}

void DevicesDialog::showDeviceContextMenu(const QPoint &pos)
{
    int row = m_table->rowAt(pos.y());
    if (row < 0 || !m_rowToSerial.contains(row))
        return;

    QString serial = m_rowToSerial[row];

    QMenu menu(this);
    menu.addAction(tr("Reiniciar dispositivo"), [this, serial]() {
        emit restartDeviceRequested(serial);
    });
    menu.addAction(tr("Conectar"), [this, serial]() {
        emit connectDeviceRequested(serial);
    });
    menu.addAction(tr("Desconectar"), [this, serial]() {
        emit disconnectDeviceRequested(serial);
    });

    menu.exec(m_table->viewport()->mapToGlobal(pos));
}

void DevicesDialog::rebuildTable()
{
    m_table->setRowCount(0);
    m_rowToSerial.clear();

    QList<DeviceInfo> filtered = filteredDevices();
    int totalPages = (filtered.size() + m_perPage - 1) / m_perPage;
    if (m_currentPage > totalPages && totalPages > 0) {
        m_currentPage = totalPages;
    }
    if (m_currentPage < 1) {
        m_currentPage = 1;
    }

    int start = (m_currentPage - 1) * m_perPage;
    int end = qMin(start + m_perPage, filtered.size());

    for (int i = start; i < end; ++i) {
        const auto &dev = filtered[i];
        int row = m_table->rowCount();
        m_table->insertRow(row);
        m_rowToSerial[row] = dev.serial;

        // Column 0: checkbox
        auto *checkWidget = new QWidget;
        auto *checkLayout = new QHBoxLayout(checkWidget);
        checkLayout->setContentsMargins(0, 0, 0, 0);
        checkLayout->setAlignment(Qt::AlignCenter);
        auto *checkbox = new QCheckBox;
        checkbox->setChecked(m_selectedSerials.contains(dev.serial));
        connect(checkbox, &QCheckBox::toggled, [this, serial = dev.serial](bool checked) {
            if (checked) {
                m_selectedSerials.insert(serial);
            } else {
                m_selectedSerials.remove(serial);
            }
        });
        checkLayout->addWidget(checkbox);
        m_table->setCellWidget(row, kIconColumn, checkWidget);

        // Column 1: index
        auto *indexLabel = new QLabel(QString::number(dev.index));
        indexLabel->setAlignment(Qt::AlignCenter);
        m_table->setCellWidget(row, kIndexColumn, indexLabel);

        // Column 2: model
        auto *modelLabel = new QLabel(dev.model);
        modelLabel->setStyleSheet("padding-left:8px; color:#f1f5f9; font-weight:600;");
        m_table->setCellWidget(row, kModelColumn, modelLabel);

        // Column 3: serial (IP:port) with a USB/WiFi connection-type badge.
        const bool isWifi = dev.serial.contains(':');
        auto *serialWidget = new QWidget;
        auto *serialRow = new QHBoxLayout(serialWidget);
        serialRow->setContentsMargins(8, 0, 8, 0);
        serialRow->setSpacing(8);
        auto *connBadge = new QLabel(isWifi ? tr("WiFi") : tr("USB"));
        connBadge->setAlignment(Qt::AlignCenter);
        connBadge->setStyleSheet(
            isWifi
                ? QStringLiteral("background:#2563eb; color:#ffffff; font-size:10px; "
                                 "font-weight:bold; border-radius:3px; padding:2px 7px;")
                : QStringLiteral("background:#f59e0b; color:#1a1206; font-size:10px; "
                                 "font-weight:bold; border-radius:3px; padding:2px 7px;"));
        auto *serialLabel = new QLabel(dev.ipPort);
        serialLabel->setStyleSheet("color:#94a3b8; font-family:monospace;");
        serialRow->addWidget(connBadge);
        serialRow->addWidget(serialLabel);
        serialRow->addStretch();
        m_table->setCellWidget(row, kSerialColumn, serialWidget);

        // Column 4: Reiniciar button
        auto *restartWidget = new QWidget;
        auto *restartLayout = new QHBoxLayout(restartWidget);
        restartLayout->setContentsMargins(8, 8, 8, 8);
        restartLayout->setSpacing(0);
        auto *restartBtn = new QPushButton(tr("Reiniciar"));
        restartBtn->setMinimumWidth(100);
        // Own stylesheet: the global QPushButton padding+min-height would
        // overflow a fixed height and clip the rounded bottom corners.
        restartBtn->setStyleSheet(
            "QPushButton { background:#4169e1; border:none; border-radius:5px; "
            "color:#ffffff; font-size:12px; padding:0 12px; min-height:0; } "
            "QPushButton:hover { background:#5a7df5; }");
        restartBtn->setMinimumHeight(34);
        connect(restartBtn, &QPushButton::clicked, [this, serial = dev.serial]() {
            emit restartDeviceRequested(serial);
        });
        restartLayout->addWidget(restartBtn);
        m_table->setCellWidget(row, kActionColumn, restartWidget);

        // Column 5: three-dot menu
        auto *menuWidget = new QWidget;
        auto *menuLayout = new QHBoxLayout(menuWidget);
        menuLayout->setContentsMargins(0, 0, 0, 0);
        menuLayout->setAlignment(Qt::AlignCenter);
        auto *menuBtn = new QPushButton("⋯");
        menuBtn->setFixedSize(28, 28);
        menuBtn->setStyleSheet("QPushButton { border:none; color:#7c8aa0; font-size:16px; padding:0; } "
                               "QPushButton:hover { color:#e2e8f0; background:#1e2636; }");
        connect(menuBtn, &QPushButton::clicked, [this, row, serial = dev.serial]() {
            QPoint pos = m_table->visualItemRect(m_table->item(row, 0)).bottomLeft();
            showDeviceContextMenu(pos);
        });
        menuLayout->addWidget(menuBtn);
        m_table->setCellWidget(row, kMenuColumn, menuWidget);

        m_table->setRowHeight(row, 56);
    }

    updatePagination();
}

void DevicesDialog::updatePagination()
{
    int total = filteredDevices().size();
    int totalPages = (total + m_perPage - 1) / m_perPage;
    if (totalPages < 1) {
        totalPages = 1;
    }

    m_paginationLabel->setText(QString::number(m_currentPage));
    m_prevPageButton->setEnabled(m_currentPage > 1);
    m_nextPageButton->setEnabled(m_currentPage < totalPages);
}

void DevicesDialog::updateTotalLabel()
{
    m_totalLabel->setText(tr("Total: %1").arg(m_devices.size()));
}

QList<DeviceInfo> DevicesDialog::filteredDevices() const
{
    QList<DeviceInfo> result;
    for (const auto &dev : m_devices) {
        if (!m_searchText.isEmpty()) {
            bool match = dev.model.contains(m_searchText, Qt::CaseInsensitive)
                         || dev.serial.contains(m_searchText, Qt::CaseInsensitive)
                         || dev.ipPort.contains(m_searchText, Qt::CaseInsensitive);
            if (!match) {
                continue;
            }
        }
        result.append(dev);
    }
    return result;
}
