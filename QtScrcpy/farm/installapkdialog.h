#ifndef FARM_INSTALLAPKDIALOG_H
#define FARM_INSTALLAPKDIALOG_H

#include <QDialog>
#include <QList>
#include <QPair>
#include <QString>

class QLineEdit;
class QPushButton;

/**
 * Install APK dialog, styled like GenFarmer: pick a local .apk and install it
 * onto the target devices (shown as numbered phone chips). Returns the chosen
 * path via apkPath() when accepted.
 */
class InstallApkDialog : public QDialog
{
    Q_OBJECT
public:
    // targets: (serial, display number) of the devices the apk installs onto.
    explicit InstallApkDialog(const QList<QPair<QString, int>> &targets,
                              QWidget *parent = nullptr);
    ~InstallApkDialog() override;

    QString apkPath() const;

private slots:
    void browseForApk();
    void onInstallClicked();

private:
    void setupUi(const QList<QPair<QString, int>> &targets);

    QLineEdit *m_pathEdit = nullptr;
    QPushButton *m_installButton = nullptr;
};

#endif // FARM_INSTALLAPKDIALOG_H
