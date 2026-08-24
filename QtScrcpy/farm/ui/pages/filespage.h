#ifndef FARM_UI_FILESPAGE_H
#define FARM_UI_FILESPAGE_H

#include <QStringList>
#include <QWidget>

#include "adb/adbparsers.h"

class QComboBox;
class QTableWidget;
class QLineEdit;
class QLabel;

namespace farm {

/**
 * Remote file manager: browse one device (shortcuts for Download / DCIM /
 * Pictures), pull, delete, create folders; upload files to the target selection
 * (or all online devices) with bounded concurrency and per-device results.
 */
class FilesPage : public QWidget
{
    Q_OBJECT
public:
    explicit FilesPage(QWidget *parent = nullptr);
    void setTargets(const QStringList &ids);

protected:
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dropEvent(QDropEvent *event) override;

private:
    QStringList targetIds() const;
    QString referenceId() const;
    void navigate(const QString &path);
    void reload();
    QStringList selectedRemotePaths() const;

    QStringList m_targets;
    QString m_path = QStringLiteral("/sdcard");
    QComboBox *m_reference = nullptr;
    QComboBox *m_scope = nullptr;
    QLineEdit *m_pathEdit = nullptr;
    QTableWidget *m_table = nullptr;
    QLabel *m_status = nullptr;
    QLabel *m_targetLabel = nullptr;
    QList<adb::RemoteEntry> m_entries;
};

} // namespace farm

#endif // FARM_UI_FILESPAGE_H
