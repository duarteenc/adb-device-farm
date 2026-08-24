#ifndef FARM_UI_APPSPAGE_H
#define FARM_UI_APPSPAGE_H

#include <QStringList>
#include <QWidget>

#include "adb/adbparsers.h"

class QComboBox;
class QTableWidget;
class QLineEdit;
class QLabel;
class QCheckBox;
class QPlainTextEdit;

namespace farm {

/**
 * Application manager: package list of a reference device, batch install /
 * uninstall / launch / force-stop / clear data / permissions on the target
 * selection (or all online devices), drag-and-drop APK install.
 */
class AppsPage : public QWidget
{
    Q_OBJECT
public:
    explicit AppsPage(QWidget *parent = nullptr);
    void setTargets(const QStringList &ids);

protected:
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dropEvent(QDropEvent *event) override;

private:
    QStringList targetIds() const;
    QString referenceId() const;
    void reloadPackages();
    void showDetails(const QString &package);
    void filterRows();
    QString selectedPackage() const;

    QStringList m_targets;
    QComboBox *m_reference = nullptr;
    QComboBox *m_scope = nullptr;
    QLineEdit *m_filter = nullptr;
    QCheckBox *m_thirdParty = nullptr;
    QTableWidget *m_table = nullptr;
    QLabel *m_targetLabel = nullptr;
    QLabel *m_status = nullptr;
    QPlainTextEdit *m_details = nullptr;
    QList<adb::PackageInfo> m_packages;
};

} // namespace farm

#endif // FARM_UI_APPSPAGE_H
