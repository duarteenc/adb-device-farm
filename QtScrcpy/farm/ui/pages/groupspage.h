#ifndef FARM_UI_GROUPSPAGE_H
#define FARM_UI_GROUPSPAGE_H

#include <QWidget>

class QListWidget;
class QTableWidget;
class QLabel;
class QComboBox;
class QSpinBox;
class QCheckBox;

namespace farm {

/**
 * Group management: create / rename / delete / colour, drag devices between
 * groups (drag from the member table onto a group in the list), bulk move,
 * per-group keep-awake and quality profile.
 */
class GroupsPage : public QWidget
{
    Q_OBJECT
public:
    explicit GroupsPage(QWidget *parent = nullptr);

signals:
    void showGroupRequested(const QString &group);
    void selectGroupRequested(const QString &group);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void reloadGroups();
    void reloadMembers();
    void saveGroupSettings();
    QString currentGroup() const;
    QStringList selectedMemberIds() const;

    QListWidget *m_groups = nullptr;
    QTableWidget *m_members = nullptr;
    QTableWidget *m_unassigned = nullptr;
    QLabel *m_title = nullptr;
    QComboBox *m_keepAwake = nullptr;
    QComboBox *m_preset = nullptr;
    QSpinBox *m_fps = nullptr;
    QSpinBox *m_bitrate = nullptr;
    QSpinBox *m_maxSize = nullptr;
    bool m_loadingSettings = false;
};

} // namespace farm

#endif // FARM_UI_GROUPSPAGE_H
