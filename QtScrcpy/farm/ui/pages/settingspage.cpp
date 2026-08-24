#include "settingspage.h"

#include <QDir>
#include <QNetworkRequest>
#include <QCheckBox>
#include <QComboBox>
#include <QDesktopServices>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QPushButton>
#include <QScrollArea>
#include <QSpinBox>
#include <QStackedWidget>
#include <QUrl>
#include <QVBoxLayout>

#include "adb/adbexecutor.h"
#include "core/appcontext.h"
#include "core/farmlog.h"
#include "core/farmsettings.h"
#include "core/ipv4.h"
#include "core/taskexecutor.h"
#include "ui/farmtheme.h"

namespace farm {

namespace {
FarmSettings &S()
{
    return FarmSettings::instance();
}

QCheckBox *check(const QString &label, const QString &key, bool def, QWidget *parent)
{
    auto *c = new QCheckBox(label, parent);
    c->setChecked(S().boolValue(key, def));
    QObject::connect(c, &QCheckBox::toggled, parent, [key](bool on) { S().setValue(key, on); });
    return c;
}

QSpinBox *spin(const QString &key, int def, int lo, int hi, QWidget *parent, const QString &suffix = QString())
{
    auto *s = new QSpinBox(parent);
    s->setRange(lo, hi);
    s->setValue(S().intValue(key, def));
    s->setSuffix(suffix);
    QObject::connect(s, &QSpinBox::valueChanged, parent, [key](int v) { S().setValue(key, v); });
    return s;
}

QLineEdit *edit(const QString &key, const QString &def, QWidget *parent, const QString &placeholder = QString())
{
    auto *e = new QLineEdit(S().stringValue(key, def), parent);
    e->setPlaceholderText(placeholder);
    QObject::connect(e, &QLineEdit::editingFinished, parent, [key, e]() { S().setValue(key, e->text().trimmed()); });
    return e;
}

QComboBox *combo(const QString &key, const QString &def, const QList<QPair<QString, QString>> &items, QWidget *parent)
{
    auto *c = new QComboBox(parent);
    for (const auto &it : items) {
        c->addItem(it.first, it.second);
    }
    c->setCurrentIndex(std::max(0, c->findData(S().stringValue(key, def))));
    QObject::connect(c, &QComboBox::currentIndexChanged, parent, [key, c](int) { S().setValue(key, c->currentData().toString()); });
    return c;
}

QWidget *form(QWidget *parent, const QString &title, const QString &intro, QFormLayout **out)
{
    auto *page = new QWidget(parent);
    auto *v = new QVBoxLayout(page);
    v->setContentsMargins(16, 12, 16, 12);
    auto *t = new QLabel(title, page);
    t->setStyleSheet(QStringLiteral("font-size:16px; font-weight:bold;"));
    v->addWidget(t);
    if (!intro.isEmpty()) {
        v->addWidget(theme::hint(intro, page));
    }
    auto *f = new QFormLayout();
    f->setLabelAlignment(Qt::AlignRight);
    f->setHorizontalSpacing(14);
    f->setVerticalSpacing(8);
    v->addLayout(f);
    v->addStretch(1);
    *out = f;
    return page;
}
} // namespace

SettingsPage::SettingsPage(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("page"));
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(20, 16, 20, 16);
    auto *title = new QLabel(tr("Settings"), this);
    title->setObjectName(QStringLiteral("pageTitle"));
    root->addWidget(title);
    root->addWidget(theme::hint(tr("Changes apply immediately and are saved to %1").arg(QDir::toNativeSeparators(S().settingsFile())), this));
    auto *body = new QHBoxLayout();
    m_categories = new QListWidget(this);
    m_categories->setFixedWidth(180);
    m_stack = new QStackedWidget(this);
    body->addWidget(m_categories);
    body->addWidget(m_stack, 1);
    root->addLayout(body, 1);
    addCategory(tr("General"), general());
    addCategory(tr("Device Discovery"), discovery());
    addCategory(tr("ADB"), adb());
    addCategory(tr("Mirroring"), mirroring());
    addCategory(tr("Performance"), performance());
    addCategory(tr("Keep Awake"), keepAwake());
    addCategory(tr("Automation"), automation());
    addCategory(tr("Scheduler"), scheduler());
    addCategory(tr("Storage"), storage());
    addCategory(tr("Notifications"), notifications());
    addCategory(tr("Advanced"), advanced());
    connect(m_categories, &QListWidget::currentRowChanged, m_stack, &QStackedWidget::setCurrentIndex);
    m_categories->setCurrentRow(0);
}

void SettingsPage::addCategory(const QString &name, QWidget *page)
{
    m_categories->addItem(name);
    auto *scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidget(page);
    m_stack->addWidget(scroll);
}

QWidget *SettingsPage::general()
{
    QFormLayout *f = nullptr;
    QWidget *page = form(this, tr("General"), QString(), &f);
    f->addRow(tr("Theme"), combo(QStringLiteral("general/theme"), QStringLiteral("dark"), { { tr("Dark"), QStringLiteral("dark") }, { tr("Light"), QStringLiteral("light") } }, page));
    f->addRow(QString(), theme::hint(tr("Theme changes apply after restarting the application."), page));
    f->addRow(tr("Open on page"), combo(QStringLiteral("ui/lastPage"), QStringLiteral("devices"), { { tr("Dashboard"), QStringLiteral("dashboard") }, { tr("Devices"), QStringLiteral("devices") } }, page));
    f->addRow(QString(), check(tr("Start minimised to tray"), QStringLiteral("general/startMinimized"), false, page));
    f->addRow(tr("Data directory"), new QLabel(QDir::toNativeSeparators(S().dataDirectory()), page));
    auto *openData = theme::button(tr("Open data folder"), page);
    connect(openData, &QPushButton::clicked, this, []() { QDesktopServices::openUrl(QUrl::fromLocalFile(S().dataDirectory())); });
    f->addRow(QString(), openData);
    return page;
}

QWidget *SettingsPage::discovery()
{
    QFormLayout *f = nullptr;
    QWidget *page = form(this, tr("Device Discovery"), tr("Every start scans the LAN for ADB devices. The subnet is probed with bounded parallel TCP connects, then answering hosts get an `adb connect`."), &f);
    f->addRow(QString(), check(tr("Automatic discovery"), QStringLiteral("discovery/enabled"), true, page));
    auto *subnet = edit(QStringLiteral("discovery/subnet"), QStringLiteral("192.168.100.0/24"), page, tr("CIDR or range, e.g. 192.168.100.0/24 or 192.168.100.1-254"));
    auto *subnetHint = theme::hint(QString(), page);
    auto validate = [subnet, subnetHint]() {
        ipv4::Range r;
        if (ipv4::parseRange(subnet->text(), r)) {
            subnetHint->setText(tr("%1 hosts: %2 – %3").arg(r.count()).arg(ipv4::toString(r.first), ipv4::toString(r.last)));
        } else {
            subnetHint->setText(tr("Invalid subnet"));
        }
    };
    connect(subnet, &QLineEdit::textChanged, this, validate);
    validate();
    f->addRow(tr("Subnet"), subnet);
    f->addRow(QString(), subnetHint);
    f->addRow(tr("ADB port"), spin(QStringLiteral("discovery/adbPort"), 5555, 1, 65535, page));
    f->addRow(tr("Quick refresh (adb devices)"), spin(QStringLiteral("discovery/quickRefreshSeconds"), 4, 1, 120, page, tr(" s")));
    f->addRow(tr("Full subnet rescan"), spin(QStringLiteral("discovery/fullScanSeconds"), 45, 5, 3600, page, tr(" s")));
    f->addRow(QString(), check(tr("Adaptive: back off rescans while nothing changes"), QStringLiteral("discovery/adaptive"), true, page));
    f->addRow(tr("Probe concurrency"), spin(QStringLiteral("discovery/scanConcurrency"), 64, 4, 256, page));
    f->addRow(tr("Probe timeout"), spin(QStringLiteral("discovery/scanTimeoutMs"), 800, 200, 5000, page, tr(" ms")));
    f->addRow(QString(), check(tr("Auto connect discovered devices"), QStringLiteral("discovery/autoConnect"), true, page));
    f->addRow(QString(), check(tr("Auto mirror when a device comes online"), QStringLiteral("discovery/autoMirror"), true, page));
    f->addRow(QString(), check(tr("Use adb mDNS (wireless debugging advertisements)"), QStringLiteral("discovery/useMdns"), true, page));
    f->addRow(QString(), check(tr("Use ARP neighbour table to prioritise hosts"), QStringLiteral("discovery/useArp"), true, page));
    return page;
}

QWidget *SettingsPage::adb()
{
    QFormLayout *f = nullptr;
    QWidget *page = form(this, tr("ADB"), tr("Leave the path empty to use the adb.exe shipped next to the application."), &f);
    auto *path = edit(QStringLiteral("adb/path"), QString(), page, AdbExecutor::instance().adbPath());
    auto *browse = theme::button(tr("Browse…"), page);
    connect(browse, &QPushButton::clicked, this, [this, path]() {
        const QString p = QFileDialog::getOpenFileName(this, tr("adb.exe"), QString(), QStringLiteral("adb.exe"));
        if (!p.isEmpty()) {
            path->setText(p);
            S().setValue(QStringLiteral("adb/path"), p);
        }
    });
    auto *row = new QHBoxLayout();
    row->addWidget(path, 1);
    row->addWidget(browse);
    f->addRow(tr("adb path"), row);
    f->addRow(tr("Max concurrent adb processes"), spin(QStringLiteral("adb/concurrency"), 8, 1, 32, page));
    f->addRow(tr("Default command timeout"), spin(QStringLiteral("adb/timeoutMs"), 15000, 1000, 120000, page, tr(" ms")));
    f->addRow(tr("Max simultaneous `adb connect`"), spin(QStringLiteral("adb/connectConcurrency"), 8, 1, 32, page));
    f->addRow(tr("`adb connect` timeout"), spin(QStringLiteral("adb/connectTimeoutMs"), 6000, 1000, 30000, page, tr(" ms")));
    return page;
}

QWidget *SettingsPage::mirroring()
{
    QFormLayout *f = nullptr;
    QWidget *page = form(this, tr("Mirroring"), tr("Presets are chosen on the Devices page; these are the plumbing limits."), &f);
    f->addRow(tr("Max simultaneous mirror starts"), spin(QStringLiteral("mirror/maxConcurrentStarts"), 4, 1, 16, page));
    f->addRow(tr("Mirror start timeout"), spin(QStringLiteral("mirror/startTimeoutMs"), 20000, 5000, 120000, page, tr(" ms")));
    f->addRow(QString(), check(tr("Automatic reconnect with backoff (1, 2, 5, 10, 30, 60 s)"), QStringLiteral("mirror/autoReconnect"), true, page));
    f->addRow(tr("Max reconnect attempts (0 = unlimited)"), spin(QStringLiteral("mirror/reconnectMaxAttempts"), 0, 0, 1000, page));
    f->addRow(QString(), check(tr("Normalise resolution/density before capture (same coordinate space on every phone)"), QStringLiteral("mirror/normalizeResolution"), true, page));
    f->addRow(tr("Normalised size"), edit(QStringLiteral("mirror/normalizedSize"), QStringLiteral("1080x2220"), page));
    f->addRow(tr("Normalised density"), edit(QStringLiteral("mirror/normalizedDensity"), QStringLiteral("480"), page));
    f->addRow(QString(), check(tr("Adaptive quality when many mirrors are active"), QStringLiteral("mirror/adaptiveQuality"), true, page));
    return page;
}

QWidget *SettingsPage::performance()
{
    QFormLayout *f = nullptr;
    QWidget *page = form(this, tr("Performance"), tr("Tiles outside the scroll viewport keep decoding (so control never stops) but upload frames to the GPU at a reduced rate."), &f);
    f->addRow(tr("Off-screen tile fps (0 = freeze)"), spin(QStringLiteral("perf/offscreenFps"), 2, 0, 30, page));
    f->addRow(tr("Metrics sample interval"), spin(QStringLiteral("perf/sampleMs"), 1000, 250, 10000, page, tr(" ms")));
    f->addRow(tr("Automation lane threads"), spin(QStringLiteral("automation/concurrency"), 5, 1, 32, page));
    return page;
}

QWidget *SettingsPage::keepAwake()
{
    QFormLayout *f = nullptr;
    QWidget *page = form(this, tr("Keep Awake"), tr("svc power stayon true · stay_on_while_plugged_in 7 · screen_off_timeout max — verified after applying; failures are reported per device."), &f);
    f->addRow(QString(), check(tr("Keep devices awake"), QStringLiteral("keepawake/enabled"), true, page));
    f->addRow(QString(), check(tr("Re-apply after reconnect / reboot / discovery"), QStringLiteral("keepawake/reapplyOnReconnect"), true, page));
    f->addRow(QString(), check(tr("Wake a display that went off (KEYCODE_WAKEUP)"), QStringLiteral("keepawake/wakeSleeping"), true, page));
    f->addRow(tr("Screen check interval"), spin(QStringLiteral("keepawake/checkSeconds"), 60, 10, 3600, page, tr(" s")));
    f->addRow(QString(), theme::hint(tr("Lock screens are never bypassed: a locked device is reported as 'Awake but locked'."), page));
    f->addRow(theme::sectionTitle(tr("Health monitor"), page));
    f->addRow(QString(), check(tr("Collect battery / temperature / storage / RSSI / latency"), QStringLiteral("health/enabled"), true, page));
    f->addRow(tr("Health interval"), spin(QStringLiteral("health/intervalSeconds"), 45, 10, 3600, page, tr(" s")));
    f->addRow(tr("Battery low threshold"), spin(QStringLiteral("health/batteryLow"), 15, 1, 99, page, tr(" %")));
    f->addRow(tr("Temperature high threshold"), spin(QStringLiteral("health/temperatureHigh"), 45, 30, 80, page, tr(" °C")));
    return page;
}

QWidget *SettingsPage::automation()
{
    QFormLayout *f = nullptr;
    QWidget *page = form(this, tr("Automation"), tr("The workflow engine runs device jobs on the automation lane; per-run logs and error screenshots go to the runs directory."), &f);
    f->addRow(tr("Default device concurrency"), spin(QStringLiteral("automation/concurrency"), 5, 1, 32, page));
    f->addRow(QString(), check(tr("Capture a screenshot when a step fails"), QStringLiteral("automation/errorScreenshots"), true, page));
    f->addRow(tr("Runs directory"), edit(QStringLiteral("automation/runsDirectory"), S().automationRunsDirectory(), page));
    f->addRow(tr("OCR provider"), combo(QStringLiteral("automation/ocr"), QStringLiteral("auto"), { { tr("Auto (Windows OCR if available)"), QStringLiteral("auto") }, { tr("Windows built-in OCR"), QStringLiteral("windows") }, { tr("Disabled"), QStringLiteral("none") } }, page));
    f->addRow(theme::sectionTitle(tr("Natural-language assistant (optional, local first)"), page));
    f->addRow(tr("Provider"), combo(QStringLiteral("ai/provider"), QStringLiteral("none"), { { tr("None"), QStringLiteral("none") }, { tr("Ollama (localhost)"), QStringLiteral("ollama") }, { tr("OpenAI-compatible endpoint"), QStringLiteral("openai") } }, page));
    f->addRow(tr("Endpoint"), edit(QStringLiteral("ai/endpoint"), QStringLiteral("http://localhost:11434"), page));
    f->addRow(tr("Model"), edit(QStringLiteral("ai/model"), QStringLiteral("llama3.1"), page));
    f->addRow(tr("API key (only for hosted endpoints)"), edit(QStringLiteral("ai/apiKey"), QString(), page));
    m_aiStatus = theme::hint(tr("The core product never requires an AI service. Install Ollama from https://ollama.com and `ollama pull llama3.1` to enable local generation."), page);
    auto *test = theme::button(tr("Test connection"), page);
    connect(test, &QPushButton::clicked, this, [this]() {
        auto *nam = new QNetworkAccessManager(this);
        const QString base = S().aiEndpoint();
        QNetworkReply *reply = nam->get(QNetworkRequest(QUrl(base + (S().aiProvider() == QLatin1String("ollama") ? QStringLiteral("/api/tags") : QStringLiteral("/v1/models")))));
        connect(reply, &QNetworkReply::finished, this, [this, reply, nam]() {
            m_aiStatus->setText(reply->error() == QNetworkReply::NoError ? tr("Connected: %1").arg(QString::fromUtf8(reply->readAll()).left(200)) : tr("Not reachable: %1").arg(reply->errorString()));
            reply->deleteLater();
            nam->deleteLater();
        });
    });
    f->addRow(QString(), test);
    f->addRow(QString(), m_aiStatus);
    return page;
}

QWidget *SettingsPage::scheduler()
{
    QFormLayout *f = nullptr;
    QWidget *page = form(this, tr("Scheduler"), tr("Schedules persist in the database and resume on start."), &f);
    f->addRow(QString(), check(tr("Scheduler enabled"), QStringLiteral("scheduler/enabled"), true, page));
    f->addRow(tr("Default missed-run policy"), combo(QStringLiteral("scheduler/missedPolicy"), QStringLiteral("skip"), { { tr("Skip missed run"), QStringLiteral("skip") }, { tr("Run immediately"), QStringLiteral("immediate") }, { tr("Run once at next startup"), QStringLiteral("nextStart") } }, page));
    f->addRow(tr("Tick interval"), spin(QStringLiteral("scheduler/tickSeconds"), 30, 5, 600, page, tr(" s")));
    return page;
}

QWidget *SettingsPage::storage()
{
    QFormLayout *f = nullptr;
    QWidget *page = form(this, tr("Storage"), QString(), &f);
    auto dirRow = [&](const QString &label, const QString &key, const QString &def) {
        auto *e = edit(key, def, page);
        auto *b = theme::button(tr("Browse…"), page);
        connect(b, &QPushButton::clicked, this, [this, e, key]() {
            const QString d = QFileDialog::getExistingDirectory(this, tr("Choose folder"), e->text());
            if (!d.isEmpty()) {
                e->setText(d);
                S().setValue(key, d);
            }
        });
        auto *row = new QHBoxLayout();
        row->addWidget(e, 1);
        row->addWidget(b);
        f->addRow(label, row);
    };
    dirRow(tr("Screenshots"), QStringLiteral("storage/screenshotDirectory"), S().screenshotDirectory());
    dirRow(tr("Recordings"), QStringLiteral("storage/recordingDirectory"), S().recordingDirectory());
    dirRow(tr("Automation runs"), QStringLiteral("automation/runsDirectory"), S().automationRunsDirectory());
    f->addRow(tr("Keep job history for"), spin(QStringLiteral("storage/historyDays"), 30, 1, 365, page, tr(" days")));
    f->addRow(tr("Log file size"), spin(QStringLiteral("storage/logMb"), 5, 1, 100, page, tr(" MB")));
    return page;
}

QWidget *SettingsPage::notifications()
{
    QFormLayout *f = nullptr;
    QWidget *page = form(this, tr("Notifications"), tr("Desktop notifications through the tray icon."), &f);
    f->addRow(QString(), check(tr("Enable notifications"), QStringLiteral("notify/enabled"), true, page));
    f->addRow(QString(), check(tr("Device disconnected"), QStringLiteral("notify/deviceDisconnected"), true, page));
    f->addRow(QString(), check(tr("Automation completed"), QStringLiteral("notify/automationCompleted"), true, page));
    f->addRow(QString(), check(tr("Automation failed"), QStringLiteral("notify/automationFailed"), true, page));
    f->addRow(QString(), check(tr("Battery low"), QStringLiteral("notify/batteryLow"), true, page));
    f->addRow(QString(), check(tr("Temperature high"), QStringLiteral("notify/temperatureHigh"), true, page));
    f->addRow(QString(), check(tr("APK installation failed"), QStringLiteral("notify/apkFailed"), true, page));
    return page;
}

QWidget *SettingsPage::advanced()
{
    QFormLayout *f = nullptr;
    QWidget *page = form(this, tr("Advanced"), QString(), &f);
    f->addRow(tr("Log level"), combo(QStringLiteral("advanced/logLevel"), QStringLiteral("debug"), { { QStringLiteral("debug"), QStringLiteral("debug") }, { QStringLiteral("info"), QStringLiteral("info") }, { QStringLiteral("warn"), QStringLiteral("warn") } }, page));
    f->addRow(tr("Mock devices on next start (0 = off)"), spin(QStringLiteral("advanced/mockDevices"), 0, 0, 500, page));
    f->addRow(QString(), theme::hint(tr("Mock mode simulates devices for UI/automation testing; it is clearly separated from real ADB and never reported as real-device performance."), page));
    auto *openLogs = theme::button(tr("Open log folder"), page);
    connect(openLogs, &QPushButton::clicked, this, []() { QDesktopServices::openUrl(QUrl::fromLocalFile(FarmLog::instance().directory())); });
    f->addRow(QString(), openLogs);
    f->addRow(tr("Version"), new QLabel(AppContext::instance().version(), page));
    return page;
}

} // namespace farm
