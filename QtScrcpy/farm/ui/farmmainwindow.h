#ifndef FARM_UI_FARMMAINWINDOW_H
#define FARM_UI_FARMMAINWINDOW_H

#include <QHash>
#include <QMainWindow>
#include <QTimer>

class QListWidget;
class QStackedWidget;
class QLabel;
class QLineEdit;
class QProgressBar;
class QSystemTrayIcon;

namespace farm {

class DevicesPage;
class DashboardPage;
class GroupsPage;
class AutomationsPage;
class SchedulerPage;
class AppsPage;
class FilesPage;
class AdbConsolePage;
class ActivityPage;
class PerformancePage;
class SettingsPage;

/**
 * The control center window: left navigation, top bar (global search, farm
 * counters, discovery progress), stacked pages, status bar and tray icon.
 */
class FarmMainWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit FarmMainWindow(QWidget *parent = nullptr);
    ~FarmMainWindow() override;

    void showPage(const QString &key);
    DevicesPage *devicesPage() const { return m_devices; }

signals:
    void firstDevicesReady();

protected:
    void closeEvent(QCloseEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;

private:
    QWidget *buildNav();
    QWidget *buildTopBar();
    void refreshCounters();
    void setupTray();
    void notify(const QString &title, const QString &message, bool warning);

    QListWidget *m_nav = nullptr;
    QStackedWidget *m_stack = nullptr;
    QHash<QString, int> m_pageIndex;
    QLineEdit *m_search = nullptr;
    QLabel *m_counters = nullptr;
    QLabel *m_scanLabel = nullptr;
    QProgressBar *m_scanBar = nullptr;
    QLabel *m_statusText = nullptr;
    QLabel *m_perfText = nullptr;
    QSystemTrayIcon *m_tray = nullptr;
    QTimer m_counterTimer;
    bool m_firstSignal = false;

    DevicesPage *m_devices = nullptr;
    DashboardPage *m_dashboard = nullptr;
    GroupsPage *m_groups = nullptr;
    AutomationsPage *m_automations = nullptr;
    SchedulerPage *m_scheduler = nullptr;
    AppsPage *m_apps = nullptr;
    FilesPage *m_files = nullptr;
    AdbConsolePage *m_console = nullptr;
    ActivityPage *m_activity = nullptr;
    PerformancePage *m_performance = nullptr;
    SettingsPage *m_settings = nullptr;
};

} // namespace farm

#endif // FARM_UI_FARMMAINWINDOW_H
