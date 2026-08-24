#ifndef FARM_UI_DASHBOARDPAGE_H
#define FARM_UI_DASHBOARDPAGE_H

#include <QHash>
#include <QTimer>
#include <QWidget>

class QLabel;
class QListWidget;
class QTableWidget;
class QProgressBar;

namespace farm {

/**
 * Farm overview: online/mirroring/attention counters, discovery status,
 * keep-awake summary, active jobs and the most recent activity.
 */
class DashboardPage : public QWidget
{
    Q_OBJECT
public:
    explicit DashboardPage(QWidget *parent = nullptr);

signals:
    void navigate(const QString &page);
    void deviceActivated(const QString &id);

private:
    QWidget *statCard(const QString &key, const QString &label, const QString &page);
    void refresh();

    QHash<QString, QLabel *> m_values;
    QLabel *m_discovery = nullptr;
    QLabel *m_keepAwake = nullptr;
    QLabel *m_health = nullptr;
    QTableWidget *m_jobs = nullptr;
    QListWidget *m_activity = nullptr;
    QListWidget *m_attention = nullptr;
    QTimer m_timer;
};

} // namespace farm

#endif // FARM_UI_DASHBOARDPAGE_H
