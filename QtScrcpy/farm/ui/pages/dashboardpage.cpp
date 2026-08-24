#include "dashboardpage.h"

#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

#include "core/activitylog.h"
#include "core/appcontext.h"
#include "core/batchjob.h"
#include "core/farmsettings.h"
#include "devices/deviceregistry.h"
#include "devices/deviceservice.h"
#include "devices/keepawakemanager.h"
#include "discovery/devicediscoveryservice.h"
#include "ui/farmtheme.h"

namespace farm {

DashboardPage::DashboardPage(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("page"));
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(20, 16, 20, 16);
    root->setSpacing(14);

    auto *title = new QLabel(tr("Dashboard"), this);
    title->setObjectName(QStringLiteral("pageTitle"));
    root->addWidget(title);

    auto *cards = new QGridLayout();
    cards->setSpacing(10);
    cards->addWidget(statCard(QStringLiteral("online"), tr("Online"), QStringLiteral("devices")), 0, 0);
    cards->addWidget(statCard(QStringLiteral("mirroring"), tr("Mirroring"), QStringLiteral("devices")), 0, 1);
    cards->addWidget(statCard(QStringLiteral("offline"), tr("Offline / known"), QStringLiteral("devices")), 0, 2);
    cards->addWidget(statCard(QStringLiteral("attention"), tr("Need attention"), QStringLiteral("devices")), 0, 3);
    cards->addWidget(statCard(QStringLiteral("groups"), tr("Groups"), QStringLiteral("groups")), 0, 4);
    cards->addWidget(statCard(QStringLiteral("jobs"), tr("Active jobs"), QStringLiteral("activity")), 0, 5);
    root->addLayout(cards);

    auto *infoRow = new QHBoxLayout();
    auto infoCard = [&](const QString &heading, QLabel **out) {
        auto *card = new QWidget(this);
        card->setObjectName(QStringLiteral("card"));
        auto *l = new QVBoxLayout(card);
        l->setContentsMargins(14, 10, 14, 10);
        l->addWidget(theme::sectionTitle(heading, card));
        auto *v = new QLabel(card);
        v->setWordWrap(true);
        l->addWidget(v);
        *out = v;
        infoRow->addWidget(card, 1);
    };
    infoCard(tr("Discovery"), &m_discovery);
    infoCard(tr("Keep awake"), &m_keepAwake);
    infoCard(tr("Fleet health"), &m_health);
    root->addLayout(infoRow);

    auto *bottom = new QHBoxLayout();
    auto listCard = [&](const QString &heading, QWidget *content, int stretch) {
        auto *card = new QWidget(this);
        card->setObjectName(QStringLiteral("card"));
        auto *l = new QVBoxLayout(card);
        l->setContentsMargins(14, 10, 14, 10);
        l->addWidget(theme::sectionTitle(heading, card));
        l->addWidget(content, 1);
        bottom->addWidget(card, stretch);
    };
    m_jobs = new QTableWidget(0, 4, this);
    m_jobs->setHorizontalHeaderLabels({ tr("Job"), tr("Status"), tr("Progress"), tr("Summary") });
    m_jobs->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Stretch);
    m_jobs->verticalHeader()->setVisible(false);
    m_jobs->setEditTriggers(QAbstractItemView::NoEditTriggers);
    listCard(tr("Jobs"), m_jobs, 3);
    m_attention = new QListWidget(this);
    connect(m_attention, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem *it) { emit deviceActivated(it->data(Qt::UserRole).toString()); });
    listCard(tr("Devices needing attention"), m_attention, 2);
    m_activity = new QListWidget(this);
    connect(m_activity, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem *it) {
        const QString id = it->data(Qt::UserRole).toString();
        if (!id.isEmpty()) {
            emit deviceActivated(id);
        }
    });
    listCard(tr("Recent activity"), m_activity, 3);
    root->addLayout(bottom, 1);

    connect(&ActivityLog::instance(), &ActivityLog::entryAdded, this, [this](const ActivityEntry &e) {
        auto *item = new QListWidgetItem(QStringLiteral("%1  %2").arg(e.time.toString(QStringLiteral("HH:mm:ss")), e.message));
        item->setData(Qt::UserRole, e.device);
        if (e.level == ActivityEntry::Warning) {
            item->setForeground(theme::warning());
        } else if (e.level == ActivityEntry::Error) {
            item->setForeground(theme::danger());
        }
        m_activity->insertItem(0, item);
        while (m_activity->count() > 60) {
            delete m_activity->takeItem(m_activity->count() - 1);
        }
    });
    const QList<ActivityEntry> recent = ActivityLog::instance().entries(40);
    for (const ActivityEntry &e : recent) {
        auto *item = new QListWidgetItem(QStringLiteral("%1  %2").arg(e.time.toString(QStringLiteral("HH:mm:ss")), e.message));
        item->setData(Qt::UserRole, e.device);
        m_activity->insertItem(0, item);
    }
    connect(&JobManager::instance(), &JobManager::jobsChanged, this, &DashboardPage::refresh);
    m_timer.setInterval(2000);
    connect(&m_timer, &QTimer::timeout, this, &DashboardPage::refresh);
    m_timer.start();
    refresh();
}

QWidget *DashboardPage::statCard(const QString &key, const QString &label, const QString &page)
{
    auto *card = new QPushButton(this);
    card->setObjectName(QStringLiteral("card"));
    card->setCursor(Qt::PointingHandCursor);
    card->setMinimumHeight(84);
    auto *l = new QVBoxLayout(card);
    l->setContentsMargins(14, 10, 14, 10);
    auto *value = new QLabel(QStringLiteral("0"), card);
    value->setObjectName(QStringLiteral("statValue"));
    auto *name = new QLabel(label, card);
    name->setObjectName(QStringLiteral("statLabel"));
    l->addWidget(value);
    l->addWidget(name);
    m_values.insert(key, value);
    connect(card, &QPushButton::clicked, this, [this, page]() { emit navigate(page); });
    return card;
}

void DashboardPage::refresh()
{
    const DeviceRegistry &registry = DeviceRegistry::instance();
    const int online = static_cast<int>(registry.onlineIds().size());
    const int mirroring = AppContext::instance().isMock() ? registry.countInState(DeviceState::Mirroring) : DeviceService::instance().mirroringCount();
    const int known = registry.count();
    const int attention = registry.countInState(DeviceState::Unauthorized) + registry.countInState(DeviceState::Error) + registry.countInState(DeviceState::Reconnecting);
    m_values[QStringLiteral("online")]->setText(QString::number(online));
    m_values[QStringLiteral("mirroring")]->setText(QString::number(mirroring));
    m_values[QStringLiteral("offline")]->setText(QStringLiteral("%1 / %2").arg(known - online).arg(known));
    m_values[QStringLiteral("attention")]->setText(QString::number(attention));
    m_values[QStringLiteral("attention")]->setStyleSheet(attention > 0 ? QStringLiteral("color:%1;").arg(theme::warning().name()) : QString());
    m_values[QStringLiteral("groups")]->setText(QString::number(registry.groups().size()));
    m_values[QStringLiteral("jobs")]->setText(QString::number(JobManager::instance().activeCount()));

    const DeviceDiscoveryService &d = DeviceDiscoveryService::instance();
    const FarmSettings &s = FarmSettings::instance();
    m_discovery->setText(tr("Subnet %1 · port %2 · auto discovery %3 · auto connect %4\n%5")
                             .arg(s.subnet())
                             .arg(s.adbPort())
                             .arg(s.autoDiscovery() ? tr("ON") : tr("OFF"), s.autoConnect() ? tr("ON") : tr("OFF"))
                             .arg(d.isScanning() ? tr("Scanning… %1 / %2").arg(d.scanDone()).arg(d.scanTotal())
                                                 : d.lastFullScan().isValid() ? tr("Last full scan %1: %2 hosts answering in %3 ms").arg(d.lastFullScan().toString(QStringLiteral("HH:mm:ss"))).arg(d.lastScanFound()).arg(d.lastScanDurationMs())
                                                                               : tr("No scan yet")));
    const KeepAwakeManager &k = KeepAwakeManager::instance();
    m_keepAwake->setText(tr("Policy %1 · %2 active · %3 failed\nWake sleeping displays: %4 · check every %5 s")
                             .arg(s.keepAwake() ? tr("ON") : tr("OFF"))
                             .arg(k.activeCount())
                             .arg(k.failedCount())
                             .arg(s.wakeSleepingDevices() ? tr("ON") : tr("OFF"))
                             .arg(s.wakeCheckSeconds()));
    int low = 0;
    int hot = 0;
    int screenOff = 0;
    double latencySum = 0;
    int latencyN = 0;
    m_attention->clear();
    for (const DeviceRecord &r : registry.all()) {
        if (r.battery >= 0 && r.battery <= s.batteryLowThreshold()) {
            ++low;
        }
        if (r.temperatureC >= s.temperatureHighThreshold()) {
            ++hot;
        }
        if (r.isOnline() && !r.screenOn) {
            ++screenOff;
        }
        if (r.latencyMs >= 0) {
            latencySum += r.latencyMs;
            ++latencyN;
        }
        QString why;
        if (r.state == DeviceState::Unauthorized) {
            why = tr("unauthorized — approve USB debugging on the phone");
        } else if (r.state == DeviceState::Error) {
            why = tr("error: %1").arg(r.stateMessage);
        } else if (r.state == DeviceState::Reconnecting) {
            why = tr("reconnecting (%1)").arg(r.stateMessage);
        } else if (r.keepAwakeStatus.startsWith(QLatin1String("Failed"))) {
            why = tr("keep-awake %1").arg(r.keepAwakeStatus);
        } else if (r.battery >= 0 && r.battery <= s.batteryLowThreshold()) {
            why = tr("battery %1%").arg(r.battery);
        }
        if (!why.isEmpty()) {
            auto *item = new QListWidgetItem(QStringLiteral("%1 %2 — %3").arg(r.numberString(), r.displayName(), why), m_attention);
            item->setData(Qt::UserRole, r.id);
        }
    }
    m_health->setText(tr("%1 low battery · %2 hot · %3 screens off · avg ADB round-trip %4")
                          .arg(low)
                          .arg(hot)
                          .arg(screenOff)
                          .arg(latencyN > 0 ? tr("%1 ms").arg(latencySum / latencyN, 0, 'f', 0) : tr("n/a")));

    const QList<BatchJob *> jobs = JobManager::instance().jobs();
    m_jobs->setRowCount(static_cast<int>(std::min<qsizetype>(jobs.size(), 12)));
    for (int i = 0; i < m_jobs->rowCount(); ++i) {
        BatchJob *j = jobs.at(i);
        m_jobs->setItem(i, 0, new QTableWidgetItem(j->name()));
        auto *st = new QTableWidgetItem(BatchJob::statusName(j->status()));
        st->setForeground(j->status() == BatchJob::Failed ? theme::danger() : j->status() == BatchJob::Running ? theme::success() : theme::textMuted());
        m_jobs->setItem(i, 1, st);
        m_jobs->setItem(i, 2, new QTableWidgetItem(QStringLiteral("%1%").arg(j->percent())));
        m_jobs->setItem(i, 3, new QTableWidgetItem(j->summary()));
    }
}

} // namespace farm
