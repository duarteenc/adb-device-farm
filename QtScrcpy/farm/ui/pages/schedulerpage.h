#ifndef FARM_UI_SCHEDULERPAGE_H
#define FARM_UI_SCHEDULERPAGE_H

#include <QWidget>

#include "scheduler/scheduler.h"

class QTableWidget;
class QLineEdit;
class QComboBox;
class QSpinBox;
class QDateTimeEdit;
class QTimeEdit;
class QCheckBox;
class QLabel;
class QDoubleSpinBox;

namespace farm {

/**
 * Scheduler page: list of schedules with next/last run, and an editor for
 * time-based and event-based triggers with missed-run policy.
 */
class SchedulerPage : public QWidget
{
    Q_OBJECT
public:
    explicit SchedulerPage(QWidget *parent = nullptr);

private:
    void reload();
    void loadIntoForm(const Schedule &s);
    Schedule formToSchedule() const;
    void updateFormVisibility();

    QTableWidget *m_table = nullptr;
    QString m_editingId;
    QLineEdit *m_name = nullptr;
    QComboBox *m_workflow = nullptr;
    QComboBox *m_targets = nullptr;
    QComboBox *m_group = nullptr;
    QSpinBox *m_concurrency = nullptr;
    QComboBox *m_kind = nullptr;
    QDateTimeEdit *m_onceAt = nullptr;
    QSpinBox *m_interval = nullptr;
    QTimeEdit *m_time = nullptr;
    QList<QCheckBox *> m_weekdays;
    QWidget *m_weekdaysRow = nullptr;
    QComboBox *m_eventType = nullptr;
    QDoubleSpinBox *m_threshold = nullptr;
    QSpinBox *m_cooldown = nullptr;
    QComboBox *m_missed = nullptr;
    QCheckBox *m_enabled = nullptr;
    QLabel *m_preview = nullptr;
};

} // namespace farm

#endif // FARM_UI_SCHEDULERPAGE_H
