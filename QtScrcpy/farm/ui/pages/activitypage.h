#ifndef FARM_UI_ACTIVITYPAGE_H
#define FARM_UI_ACTIVITYPAGE_H

#include <QWidget>

#include "core/activitylog.h"

class QTableWidget;
class QComboBox;
class QLineEdit;
class QCheckBox;

namespace farm {

/**
 * Activity Center: the event feed with level / category / device filters,
 * export to a text file, and the live job list.
 */
class ActivityPage : public QWidget
{
    Q_OBJECT
public:
    explicit ActivityPage(QWidget *parent = nullptr);

signals:
    void deviceActivated(const QString &id);

private:
    void rebuild();
    bool passes(const ActivityEntry &e) const;
    void appendRow(const ActivityEntry &e);

    QTableWidget *m_table = nullptr;
    QComboBox *m_level = nullptr;
    QComboBox *m_category = nullptr;
    QLineEdit *m_device = nullptr;
    QCheckBox *m_autoScroll = nullptr;
};

} // namespace farm

#endif // FARM_UI_ACTIVITYPAGE_H
