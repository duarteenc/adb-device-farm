#include "deviceinspector.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTabWidget>
#include <QTableWidget>
#include <QVBoxLayout>

#include "adb/adbexecutor.h"
#include "core/activitylog.h"
#include "devices/devicecommands.h"
#include "devices/devicehealthmonitor.h"
#include "devices/deviceregistry.h"
#include "devices/deviceservice.h"
#include "devices/keepawakemanager.h"
#include "ui/farmtheme.h"
#include "ui/widgets/batchjobdialog.h"

namespace farm {

DeviceInspector::DeviceInspector(const QString &id, QWidget *parent)
    : QDialog(parent)
    , m_id(id)
{
    const DeviceRecord r = DeviceRegistry::instance().get(id);
    setWindowTitle(tr("%1 %2 — %3").arg(r.numberString(), r.displayName(), id));
    resize(820, 600);
    theme::apply(this);
    auto *root = new QVBoxLayout(this);
    auto *tabs = new QTabWidget(this);
    tabs->addTab(overviewTab(), tr("Overview"));
    tabs->addTab(propertiesTab(), tr("Properties"));
    tabs->addTab(appsTab(), tr("Applications"));
    tabs->addTab(consoleTab(), tr("Console"));
    tabs->addTab(logsTab(), tr("Logs"));
    root->addWidget(tabs);
    connect(&DeviceRegistry::instance(), &DeviceRegistry::deviceChanged, this, [this](const QString &changed) {
        if (changed == m_id) {
            refreshOverview();
        }
    });
    connect(&DeviceRegistry::instance(), &DeviceRegistry::deviceRemoved, this, [this](const QString &removed) {
        if (removed == m_id) {
            close();
        }
    });
    refreshOverview();
}

QWidget *DeviceInspector::overviewTab()
{
    auto *w = new QWidget(this);
    auto *h = new QHBoxLayout(w);
    m_overview = new QLabel(w);
    m_overview->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    m_overview->setTextFormat(Qt::RichText);
    m_overview->setWordWrap(true);
    h->addWidget(m_overview, 1);

    auto *form = new QFormLayout();
    const DeviceRecord r = DeviceRegistry::instance().get(m_id);
    m_name = new QLineEdit(r.friendlyName, w);
    m_number = new QLineEdit(QString::number(r.number), w);
    m_keepAwake = new QComboBox(w);
    m_keepAwake->addItem(tr("Inherit"), -1);
    m_keepAwake->addItem(tr("ON"), 1);
    m_keepAwake->addItem(tr("OFF"), 0);
    m_keepAwake->setCurrentIndex(std::max(0, m_keepAwake->findData(r.keepAwake)));
    m_preset = new QComboBox(w);
    m_preset->addItem(tr("Inherit"), QString());
    m_preset->addItem(tr("Performance"), QStringLiteral("performance"));
    m_preset->addItem(tr("Balanced"), QStringLiteral("balanced"));
    m_preset->addItem(tr("Quality"), QStringLiteral("quality"));
    m_preset->addItem(tr("Focused (max)"), QStringLiteral("focused"));
    m_preset->setCurrentIndex(std::max(0, m_preset->findData(r.preset)));
    m_autoMirror = new QCheckBox(tr("Auto mirror when online"), w);
    m_autoMirror->setChecked(r.autoMirror);
    m_favorite = new QCheckBox(tr("Favorite"), w);
    m_favorite->setChecked(r.favorite);
    m_notes = new QPlainTextEdit(r.notes, w);
    m_notes->setFixedHeight(80);
    form->addRow(tr("Friendly name"), m_name);
    form->addRow(tr("Number"), m_number);
    form->addRow(tr("Keep awake"), m_keepAwake);
    form->addRow(tr("Quality preset"), m_preset);
    form->addRow(QString(), m_autoMirror);
    form->addRow(QString(), m_favorite);
    form->addRow(tr("Notes"), m_notes);
    auto *save = theme::button(tr("Save"), w, QStringLiteral("primary"));
    connect(save, &QPushButton::clicked, this, [this]() {
        DeviceRegistry::instance().update(m_id, [this](DeviceRecord &d) {
            d.friendlyName = m_name->text().trimmed();
            d.number = m_number->text().toInt();
            d.keepAwake = m_keepAwake->currentData().toInt();
            d.preset = m_preset->currentData().toString();
            d.autoMirror = m_autoMirror->isChecked();
            d.favorite = m_favorite->isChecked();
            d.notes = m_notes->toPlainText();
        });
        KeepAwakeManager::instance().applyPolicy(m_id);
    });
    form->addRow(save);
    auto *actions = new QHBoxLayout();
    auto act = [&](const QString &label, std::function<void()> fn) {
        auto *b = theme::button(label, w);
        connect(b, &QPushButton::clicked, this, fn);
        actions->addWidget(b);
    };
    act(tr("Mirror"), [this]() { DeviceService::instance().startMirror(m_id); });
    act(tr("Stop"), [this]() { DeviceService::instance().stopMirror(m_id); });
    act(tr("Reconnect"), [this]() { DeviceService::instance().reconnectDevice(m_id); });
    act(tr("Wake"), [this]() { KeepAwakeManager::instance().wakeDevice(m_id); });
    act(tr("Refresh health"), [this]() {
        DeviceHealthMonitor::instance().refresh(m_id);
        DeviceHealthMonitor::instance().collectIdentity(m_id);
    });
    form->addRow(actions);
    auto *formW = new QWidget(w);
    formW->setLayout(form);
    formW->setFixedWidth(330);
    h->addWidget(formW);
    return w;
}

void DeviceInspector::refreshOverview()
{
    const DeviceRecord r = DeviceRegistry::instance().get(m_id);
    const QSize fs = DeviceService::instance().frameSize(m_id);
    QString html = QStringLiteral("<table cellspacing='2' cellpadding='2'>");
    auto row = [&html](const QString &k, const QString &v) { html += QStringLiteral("<tr><td style='color:#7c8aa0'>%1</td><td><b>%2</b></td></tr>").arg(k, v.toHtmlEscaped()); };
    row(tr("ID"), r.id);
    row(tr("State"), deviceStateName(r.state) + (r.stateMessage.isEmpty() ? QString() : QStringLiteral(" — ") + r.stateMessage));
    row(tr("Connection"), connectionTypeName(r.connectionType));
    row(tr("Model"), QStringLiteral("%1 %2").arg(r.manufacturer, r.model));
    row(tr("Android"), r.androidVersion.isEmpty() ? QString() : QStringLiteral("%1 (SDK %2)").arg(r.androidVersion).arg(r.sdk));
    row(tr("Hardware serial"), r.hwSerial);
    row(tr("Group"), r.group);
    row(tr("Battery"), r.battery >= 0 ? QStringLiteral("%1%%2").arg(r.battery).arg(r.charging ? tr(" (charging)") : QString()) : tr("n/a"));
    row(tr("Temperature"), r.temperatureC > 0 ? QStringLiteral("%1 °C").arg(r.temperatureC, 0, 'f', 1) : tr("n/a"));
    row(tr("Free storage"), r.storageFreeMb >= 0 ? QStringLiteral("%1 MB").arg(r.storageFreeMb) : tr("n/a"));
    row(tr("WiFi RSSI"), r.wifiRssi < 0 ? QStringLiteral("%1 dBm").arg(r.wifiRssi) : tr("n/a"));
    row(tr("ADB round-trip"), r.latencyMs >= 0 ? QStringLiteral("%1 ms").arg(r.latencyMs) : tr("n/a"));
    row(tr("Uptime"), r.uptimeSeconds >= 0 ? QStringLiteral("%1 h").arg(r.uptimeSeconds / 3600.0, 0, 'f', 1) : tr("n/a"));
    row(tr("Screen"), QStringLiteral("%1%2").arg(r.screenOn ? tr("on") : tr("off"), r.locked ? tr(", locked") : QString()));
    row(tr("Keep awake"), r.keepAwakeStatus.isEmpty() ? tr("not applied") : r.keepAwakeStatus);
    row(tr("Stream"), fs.isEmpty() ? tr("not mirroring") : QStringLiteral("%1x%2 · %3 fps").arg(fs.width()).arg(fs.height()).arg(r.liveFps));
    row(tr("Reconnect attempts"), QString::number(r.reconnectAttempts));
    row(tr("First seen"), r.firstSeen.toString(QStringLiteral("yyyy-MM-dd HH:mm")));
    row(tr("Last seen"), r.lastSeen.toString(QStringLiteral("yyyy-MM-dd HH:mm")));
    row(tr("Last health check"), r.lastHealthCheck.isValid() ? r.lastHealthCheck.toString(QStringLiteral("HH:mm:ss")) : tr("never"));
    html += QStringLiteral("</table>");
    m_overview->setText(html);
}

QWidget *DeviceInspector::propertiesTab()
{
    auto *w = new QWidget(this);
    auto *v = new QVBoxLayout(w);
    auto *top = new QHBoxLayout();
    m_propFilter = new QLineEdit(w);
    m_propFilter->setPlaceholderText(tr("Filter properties…"));
    auto *reload = theme::button(tr("Reload"), w);
    top->addWidget(m_propFilter, 1);
    top->addWidget(reload);
    v->addLayout(top);
    m_props = new QTableWidget(0, 2, w);
    m_props->setHorizontalHeaderLabels({ tr("Property"), tr("Value") });
    m_props->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_props->verticalHeader()->setVisible(false);
    m_props->setEditTriggers(QAbstractItemView::NoEditTriggers);
    v->addWidget(m_props, 1);
    v->addWidget(theme::hint(tr("Device identity properties are read-only here; the farm never modifies them."), w));
    connect(reload, &QPushButton::clicked, this, &DeviceInspector::loadProperties);
    connect(m_propFilter, &QLineEdit::textChanged, this, [this](const QString &f) {
        for (int i = 0; i < m_props->rowCount(); ++i) {
            m_props->setRowHidden(i, !f.isEmpty() && !m_props->item(i, 0)->text().contains(f, Qt::CaseInsensitive) && !m_props->item(i, 1)->text().contains(f, Qt::CaseInsensitive));
        }
    });
    loadProperties();
    return w;
}

void DeviceInspector::loadProperties()
{
    DeviceCommands::getProperties(m_id, this, [this](QHash<QString, QString> props, QString error) {
        if (!error.isEmpty()) {
            m_props->setRowCount(1);
            m_props->setItem(0, 0, new QTableWidgetItem(tr("error")));
            m_props->setItem(0, 1, new QTableWidgetItem(error));
            return;
        }
        QStringList keys = props.keys();
        keys.sort();
        // Put the interesting ones first.
        const QStringList top{ QStringLiteral("ro.product.manufacturer"), QStringLiteral("ro.product.model"), QStringLiteral("ro.build.version.release"), QStringLiteral("ro.build.version.sdk"), QStringLiteral("ro.product.cpu.abi"), QStringLiteral("ro.serialno") };
        for (int i = static_cast<int>(top.size()) - 1; i >= 0; --i) {
            if (keys.removeOne(top.at(i))) {
                keys.prepend(top.at(i));
            }
        }
        m_props->setRowCount(static_cast<int>(keys.size()));
        for (int i = 0; i < keys.size(); ++i) {
            m_props->setItem(i, 0, new QTableWidgetItem(keys.at(i)));
            m_props->setItem(i, 1, new QTableWidgetItem(props.value(keys.at(i))));
        }
    });
}

QWidget *DeviceInspector::appsTab()
{
    auto *w = new QWidget(this);
    auto *v = new QVBoxLayout(w);
    m_apps = new QTableWidget(0, 2, w);
    m_apps->setHorizontalHeaderLabels({ tr("Package"), tr("APK") });
    m_apps->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_apps->verticalHeader()->setVisible(false);
    m_apps->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_apps->setEditTriggers(QAbstractItemView::NoEditTriggers);
    v->addWidget(m_apps, 1);
    auto *row = new QHBoxLayout();
    auto act = [&](const QString &label, std::function<void(const QString &)> fn, const QString &role = QString()) {
        auto *b = theme::button(label, w, role);
        connect(b, &QPushButton::clicked, this, [this, fn]() {
            const int r = m_apps->currentRow();
            if (r >= 0) {
                fn(m_apps->item(r, 0)->text());
            }
        });
        row->addWidget(b);
    };
    act(tr("Launch"), [this](const QString &p) { DeviceCommands::launchApp({ m_id }, p); });
    act(tr("Force stop"), [this](const QString &p) { DeviceCommands::forceStop({ m_id }, p); });
    act(tr("App info"), [this](const QString &p) { DeviceCommands::openAppInfo({ m_id }, p); });
    act(tr("Uninstall"), [this](const QString &p) { BatchJobDialog::show(DeviceCommands::uninstall({ m_id }, p), this); }, QStringLiteral("danger"));
    auto *reload = theme::button(tr("Reload"), w);
    connect(reload, &QPushButton::clicked, this, &DeviceInspector::loadApps);
    row->addWidget(reload);
    row->addStretch(1);
    v->addLayout(row);
    loadApps();
    return w;
}

void DeviceInspector::loadApps()
{
    DeviceCommands::listPackages(m_id, true, this, [this](QList<adb::PackageInfo> list, QString) {
        m_apps->setRowCount(static_cast<int>(list.size()));
        for (int i = 0; i < list.size(); ++i) {
            m_apps->setItem(i, 0, new QTableWidgetItem(list.at(i).name));
            m_apps->setItem(i, 1, new QTableWidgetItem(list.at(i).apkPath));
        }
    });
}

QWidget *DeviceInspector::consoleTab()
{
    auto *w = new QWidget(this);
    auto *v = new QVBoxLayout(w);
    auto *row = new QHBoxLayout();
    m_cmd = new QLineEdit(w);
    m_cmd->setPlaceholderText(tr("shell command, e.g. dumpsys battery"));
    auto *run = theme::button(tr("Run"), w, QStringLiteral("primary"));
    row->addWidget(m_cmd, 1);
    row->addWidget(run);
    v->addLayout(row);
    m_out = new QPlainTextEdit(w);
    m_out->setReadOnly(true);
    m_out->setFont(QFont(QStringLiteral("Consolas"), 9));
    v->addWidget(m_out, 1);
    auto exec = [this]() {
        const QString cmd = m_cmd->text().trimmed();
        if (cmd.isEmpty()) {
            return;
        }
        m_out->appendPlainText(QStringLiteral("$ ") + cmd);
        AdbExecutor::instance().shell(m_id, cmd, this, [this](const AdbResult &r) {
            m_out->appendPlainText(r.combined().trimmed().isEmpty() ? (r.ok ? tr("(no output)") : r.error) : r.combined().trimmed());
            m_out->appendPlainText(QStringLiteral("— exit %1, %2 ms").arg(r.exitCode).arg(r.durationMs));
        }, 30000);
    };
    connect(run, &QPushButton::clicked, this, exec);
    connect(m_cmd, &QLineEdit::returnPressed, this, exec);
    return w;
}

QWidget *DeviceInspector::logsTab()
{
    auto *w = new QWidget(this);
    auto *v = new QVBoxLayout(w);
    m_logs = new QListWidget(w);
    v->addWidget(m_logs, 1);
    for (const ActivityEntry &e : ActivityLog::instance().entries()) {
        if (e.device == m_id) {
            m_logs->addItem(QStringLiteral("%1  %2  %3").arg(e.time.toString(QStringLiteral("HH:mm:ss")), ActivityEntry::levelName(e.level), e.message));
        }
    }
    connect(&ActivityLog::instance(), &ActivityLog::entryAdded, this, [this](const ActivityEntry &e) {
        if (e.device == m_id) {
            m_logs->addItem(QStringLiteral("%1  %2  %3").arg(e.time.toString(QStringLiteral("HH:mm:ss")), ActivityEntry::levelName(e.level), e.message));
            m_logs->scrollToBottom();
        }
    });
    return w;
}

} // namespace farm
