#include "performancepage.h"

#include <algorithm>

#include <QFile>
#include <QPaintEvent>
#include <QPainterPath>
#include <QThread>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFileDialog>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <QPainter>
#include <QPushButton>
#include <QSysInfo>
#include <QTableWidget>
#include <QVBoxLayout>

#include "adb/adbexecutor.h"
#include "core/appcontext.h"
#include "core/farmlog.h"
#include "core/farmsettings.h"
#include "core/taskexecutor.h"
#include "devices/connectionidallocator.h"
#include "devices/deviceregistry.h"
#include "devices/deviceservice.h"
#include "storage/database.h"
#include "ui/farmtheme.h"

namespace farm {

// ---------------------------------------------------------------- SparkLine

SparkLine::SparkLine(QWidget *parent)
    : QWidget(parent)
    , m_color(theme::accent())
{
    setMinimumHeight(34);
}

void SparkLine::push(double value)
{
    m_values.append(value);
    while (m_values.size() > 120) {
        m_values.removeFirst();
    }
    update();
}

void SparkLine::paintEvent(QPaintEvent *)
{
    if (m_values.size() < 2) {
        return;
    }
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    double maxV = 1e-9;
    for (double v : m_values) {
        maxV = std::max(maxV, v);
    }
    const int n = static_cast<int>(m_values.size());
    const double w = width();
    const double h = height() - 2;
    QPainterPath path;
    for (int i = 0; i < n; ++i) {
        const double x = i * w / (120 - 1);
        const double y = h - (m_values.at(i) / maxV) * h + 1;
        if (i == 0) {
            path.moveTo(x, y);
        } else {
            path.lineTo(x, y);
        }
    }
    p.setPen(QPen(m_color, 1.5));
    p.drawPath(path);
}

// ---------------------------------------------------------------- PerformancePage

PerformancePage::PerformancePage(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("page"));
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(20, 16, 20, 16);
    root->setSpacing(12);
    auto *head = new QHBoxLayout();
    auto *title = new QLabel(tr("Performance"), this);
    title->setObjectName(QStringLiteral("pageTitle"));
    head->addWidget(title);
    head->addStretch(1);
    auto *exportBtn = theme::button(tr("Export metrics JSON…"), this);
    auto *diagBtn = theme::button(tr("Export diagnostics bundle…"), this, QStringLiteral("primary"));
    head->addWidget(exportBtn);
    head->addWidget(diagBtn);
    root->addLayout(head);

    auto *grid = new QGridLayout();
    grid->setSpacing(8);
    struct M
    {
        const char *key;
        const char *label;
        bool spark;
    };
    const M metrics[] = {
        { "cpu", "Application CPU (% of machine)", true }, { "cpu1", "CPU (% of one core)", false }, { "ram", "Working set (MB)", true },
        { "priv", "Private bytes (MB)", false },          { "handles", "Handles", false },          { "gpu", "GPU", false },
        { "devices", "Devices known / online", false },   { "mirroring", "Mirroring", true },       { "decoded", "Decoded fps (all devices)", true },
        { "rendered", "Rendered fps", true },             { "dropped", "Dropped fps (throttled)", true }, { "handle", "Avg frame handling (ms)", false },
        { "display", "Avg decode→display latency (ms)", true }, { "adbops", "ADB ops / s", true },   { "adbms", "ADB avg round-trip (ms)", true },
        { "adbq", "ADB active / queued", false },         { "connect", "Connect latency last / avg (ms)", false }, { "reconnects", "Reconnects / disconnects", false },
        { "scan", "Last network scan (ms)", false },      { "jobs", "Batch jobs / automation active / queue", false }, { "uilag", "UI event-loop stall max / avg (ms)", true },
        { "queue", "Mirror start queue / starting", false },
    };
    int i = 0;
    for (const M &m : metrics) {
        grid->addWidget(metric(QLatin1String(m.key), tr(m.label), m.spark), i / 4, i % 4);
        ++i;
    }
    root->addLayout(grid);

    m_lanes = theme::hint(QString(), this);
    root->addWidget(m_lanes);

    root->addWidget(theme::sectionTitle(tr("Per device"), this));
    m_devices = new QTableWidget(0, 7, this);
    m_devices->setHorizontalHeaderLabels({ tr("#"), tr("Device"), tr("State"), tr("Live fps"), tr("ADB latency"), tr("Reconnects"), tr("Stream") });
    m_devices->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    m_devices->horizontalHeader()->setStretchLastSection(true);
    m_devices->verticalHeader()->setVisible(false);
    m_devices->setEditTriggers(QAbstractItemView::NoEditTriggers);
    root->addWidget(m_devices, 1);

    connect(&PerfMonitor::instance(), &PerfMonitor::sampled, this, &PerformancePage::onSample);
    connect(exportBtn, &QPushButton::clicked, this, [this]() {
        const QString path = QFileDialog::getSaveFileName(this, tr("Export metrics"), QStringLiteral("farm-metrics.json"), tr("JSON (*.json)"));
        if (!path.isEmpty()) {
            PerfMonitor::instance().exportJson(path);
        }
    });
    connect(diagBtn, &QPushButton::clicked, this, &PerformancePage::exportDiagnostics);
}

QWidget *PerformancePage::metric(const QString &key, const QString &label, bool spark)
{
    auto *card = new QWidget(this);
    card->setObjectName(QStringLiteral("card"));
    auto *l = new QVBoxLayout(card);
    l->setContentsMargins(12, 8, 12, 8);
    l->setSpacing(2);
    auto *value = new QLabel(QStringLiteral("—"), card);
    value->setStyleSheet(QStringLiteral("font-size:18px; font-weight:bold;"));
    auto *name = new QLabel(label, card);
    name->setObjectName(QStringLiteral("statLabel"));
    l->addWidget(value);
    l->addWidget(name);
    if (spark) {
        auto *s = new SparkLine(card);
        l->addWidget(s);
        m_sparks.insert(key, s);
    }
    m_values.insert(key, value);
    return card;
}

void PerformancePage::onSample(const PerfSnapshot &s)
{
    auto set = [this](const QString &key, const QString &text, double sparkValue = -12345) {
        if (QLabel *l = m_values.value(key, nullptr)) {
            l->setText(text);
        }
        if (SparkLine *sp = m_sparks.value(key, nullptr); sp && sparkValue != -12345) {
            sp->push(sparkValue);
        }
    };
    set(QStringLiteral("cpu"), QStringLiteral("%1 %").arg(s.cpuPercent, 0, 'f', 1), s.cpuPercent);
    set(QStringLiteral("cpu1"), QStringLiteral("%1 %").arg(s.cpuPercentOneCore, 0, 'f', 1));
    set(QStringLiteral("ram"), QString::number(s.workingSetMb), static_cast<double>(s.workingSetMb));
    set(QStringLiteral("priv"), QString::number(s.privateMb));
    set(QStringLiteral("handles"), QString::number(s.handles));
    set(QStringLiteral("gpu"), s.gpuPercent < 0 ? tr("n/a") : QStringLiteral("%1 %").arg(s.gpuPercent, 0, 'f', 0));
    set(QStringLiteral("devices"), QStringLiteral("%1 / %2").arg(s.devicesKnown).arg(s.devicesOnline));
    set(QStringLiteral("mirroring"), QString::number(s.mirroring), s.mirroring);
    set(QStringLiteral("decoded"), QStringLiteral("%1").arg(s.decodedFps, 0, 'f', 1), s.decodedFps);
    set(QStringLiteral("rendered"), QStringLiteral("%1").arg(s.renderedFps, 0, 'f', 1), s.renderedFps);
    set(QStringLiteral("dropped"), QStringLiteral("%1").arg(s.droppedFps, 0, 'f', 1), s.droppedFps);
    set(QStringLiteral("handle"), s.avgFrameHandleMs < 0 ? QStringLiteral("—") : QStringLiteral("%1").arg(s.avgFrameHandleMs, 0, 'f', 2));
    set(QStringLiteral("display"), s.avgDisplayLatencyMs < 0 ? QStringLiteral("—") : QStringLiteral("%1").arg(s.avgDisplayLatencyMs, 0, 'f', 1), std::max(0.0, s.avgDisplayLatencyMs));
    set(QStringLiteral("adbops"), QStringLiteral("%1").arg(s.adbOpsPerSec, 0, 'f', 1), s.adbOpsPerSec);
    set(QStringLiteral("adbms"), s.adbAvgMs < 0 ? QStringLiteral("—") : QStringLiteral("%1").arg(s.adbAvgMs, 0, 'f', 0), std::max(0.0, s.adbAvgMs));
    set(QStringLiteral("adbq"), QStringLiteral("%1 / %2   (total %3, failed %4)").arg(s.adbActive).arg(s.adbQueued).arg(s.adbTotal).arg(s.adbFailed));
    set(QStringLiteral("connect"), s.lastConnectLatencyMs < 0 ? QStringLiteral("—") : QStringLiteral("%1 / %2").arg(s.lastConnectLatencyMs).arg(s.avgConnectLatencyMs, 0, 'f', 0));
    set(QStringLiteral("reconnects"), QStringLiteral("%1 / %2").arg(s.reconnects).arg(s.disconnects));
    set(QStringLiteral("scan"), s.lastScanMs < 0 ? QStringLiteral("—") : QString::number(s.lastScanMs));
    set(QStringLiteral("jobs"), QStringLiteral("%1 / %2 / %3").arg(s.batchJobs).arg(s.automationJobs).arg(s.automationQueue));
    set(QStringLiteral("uilag"), QStringLiteral("%1 / %2").arg(s.uiLagMaxMs).arg(s.uiLagAvgMs, 0, 'f', 1), s.uiLagMaxMs);
    set(QStringLiteral("queue"), QStringLiteral("%1 / %2").arg(s.connectQueue).arg(s.connectStarting));

    TaskExecutor &ex = TaskExecutor::instance();
    QStringList lanes;
    for (const QString &lane : { QStringLiteral("adb"), QStringLiteral("connect"), QStringLiteral("network"), QStringLiteral("automation"), QStringLiteral("media"), QStringLiteral("io") }) {
        lanes << QStringLiteral("%1 %2/%3 (+%4 queued)").arg(lane).arg(ex.activeCount(lane)).arg(ex.laneConcurrency(lane)).arg(ex.queuedCount(lane));
    }
    m_lanes->setText(tr("Executor lanes: %1   ·   port leases in use: %2").arg(lanes.join(QStringLiteral("  ·  "))).arg(ConnectionIdAllocator::instance().inUseCount()));
    if (isVisible()) {
        refreshDevices();
    }
}

void PerformancePage::refreshDevices()
{
    const QStringList ids = DeviceRegistry::instance().sorted(DeviceRegistry::SortKey::Number, true, DeviceRegistry::instance().onlineIds());
    m_devices->setRowCount(static_cast<int>(ids.size()));
    for (int i = 0; i < ids.size(); ++i) {
        const DeviceRecord r = DeviceRegistry::instance().get(ids.at(i));
        m_devices->setItem(i, 0, new QTableWidgetItem(r.numberString()));
        m_devices->setItem(i, 1, new QTableWidgetItem(QStringLiteral("%1  %2").arg(r.displayName(), r.id)));
        m_devices->setItem(i, 2, new QTableWidgetItem(deviceStateName(r.state)));
        m_devices->setItem(i, 3, new QTableWidgetItem(r.liveFps > 0 ? QString::number(r.liveFps) : QString()));
        m_devices->setItem(i, 4, new QTableWidgetItem(r.latencyMs >= 0 ? QStringLiteral("%1 ms").arg(r.latencyMs) : QString()));
        m_devices->setItem(i, 5, new QTableWidgetItem(QString::number(r.reconnectAttempts)));
        const QSize fs = DeviceService::instance().frameSize(r.id);
        m_devices->setItem(i, 6, new QTableWidgetItem(fs.isEmpty() ? QString() : QStringLiteral("%1x%2").arg(fs.width()).arg(fs.height())));
    }
}

void PerformancePage::exportDiagnostics()
{
    const QString dir = QFileDialog::getExistingDirectory(this, tr("Save diagnostics bundle into"));
    if (dir.isEmpty()) {
        return;
    }
    const QString bundle = QStringLiteral("%1/adb-device-farm-diagnostics-%2").arg(dir, QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss")));
    QDir().mkpath(bundle);
    // 1. logs
    QDir logDir(FarmLog::instance().directory());
    for (const QString &f : logDir.entryList({ QStringLiteral("*.log") }, QDir::Files)) {
        QFile::copy(logDir.filePath(f), bundle + QLatin1Char('/') + f);
    }
    // 2. metrics
    PerfMonitor::instance().exportJson(bundle + QStringLiteral("/metrics.json"));
    // 3. environment + devices (no secrets: settings are copied minus any *key/token*)
    QFile info(bundle + QStringLiteral("/summary.txt"));
    if (info.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        QString text;
        text += QStringLiteral("ADB Device Farm %1\nQt %2\nOS %3 (%4)\nCPU cores %5\nadb %6\n\n")
                    .arg(AppContext::instance().version(), QString::fromLatin1(qVersion()), QSysInfo::prettyProductName(), QSysInfo::currentCpuArchitecture())
                    .arg(QThread::idealThreadCount())
                    .arg(AdbExecutor::instance().adbPath());
        text += QStringLiteral("Data directory: %1\nDatabase schema: v%2\n\nDevices:\n").arg(FarmSettings::instance().dataDirectory()).arg(Database::instance().schemaVersion());
        for (const DeviceRecord &r : DeviceRegistry::instance().all()) {
            text += QStringLiteral("  %1 %2 %3 state=%4 model=%5 android=%6 group=%7 keepawake=%8 battery=%9 latency=%10\n")
                        .arg(r.numberString(), r.id, r.displayName(), deviceStateName(r.state), r.model, r.androidVersion, r.group, r.keepAwakeStatus)
                        .arg(r.battery)
                        .arg(r.latencyMs);
        }
        text += QStringLiteral("\nSettings (secrets removed):\n");
        QFile settings(FarmSettings::instance().settingsFile());
        if (settings.open(QIODevice::ReadOnly | QIODevice::Text)) {
            for (const QString &line : QString::fromUtf8(settings.readAll()).split(QLatin1Char('\n'))) {
                if (line.contains(QLatin1String("key"), Qt::CaseInsensitive) || line.contains(QLatin1String("token"), Qt::CaseInsensitive) || line.contains(QLatin1String("password"), Qt::CaseInsensitive)) {
                    continue;
                }
                text += QStringLiteral("  ") + line + QLatin1Char('\n');
            }
        }
        info.write(text.toUtf8());
    }
    // 4. database copy
    Database::instance().backupTo(bundle + QStringLiteral("/farm.db"));
    QMessageBox::information(this, tr("Diagnostics"), tr("Diagnostics written to:\n%1\n\nZip that folder when sending it for support.").arg(QDir::toNativeSeparators(bundle)));
}

} // namespace farm
