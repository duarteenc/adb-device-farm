#ifndef FARM_UI_SETTINGSPAGE_H
#define FARM_UI_SETTINGSPAGE_H

#include <QHash>
#include <QWidget>

class QListWidget;
class QStackedWidget;
class QLabel;

namespace farm {

/**
 * Settings page: General / Device Discovery / ADB / Mirroring / Performance /
 * Keep Awake / Automation / Scheduler / Storage / Notifications / Advanced.
 * Every control writes straight to FarmSettings (services react live).
 */
class SettingsPage : public QWidget
{
    Q_OBJECT
public:
    explicit SettingsPage(QWidget *parent = nullptr);

private:
    QWidget *general();
    QWidget *discovery();
    QWidget *adb();
    QWidget *mirroring();
    QWidget *performance();
    QWidget *keepAwake();
    QWidget *automation();
    QWidget *scheduler();
    QWidget *storage();
    QWidget *notifications();
    QWidget *advanced();
    void addCategory(const QString &name, QWidget *page);

    QListWidget *m_categories = nullptr;
    QStackedWidget *m_stack = nullptr;
    QLabel *m_aiStatus = nullptr;
};

} // namespace farm

#endif // FARM_UI_SETTINGSPAGE_H
