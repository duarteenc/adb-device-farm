#ifndef FARM_UI_DEVICEINSPECTOR_H
#define FARM_UI_DEVICEINSPECTOR_H

#include <QDialog>
#include <QString>

class QLabel;
class QTableWidget;
class QPlainTextEdit;
class QLineEdit;
class QListWidget;
class QCheckBox;
class QComboBox;

namespace farm {

/**
 * Per-device detail window: Overview (identity, health, keep-awake, stream,
 * per-device settings), Properties (getprop), Applications, Console, Logs.
 */
class DeviceInspector : public QDialog
{
    Q_OBJECT
public:
    explicit DeviceInspector(const QString &id, QWidget *parent = nullptr);

private:
    QWidget *overviewTab();
    QWidget *propertiesTab();
    QWidget *appsTab();
    QWidget *consoleTab();
    QWidget *logsTab();
    void refreshOverview();
    void loadProperties();
    void loadApps();

    QString m_id;
    QLabel *m_overview = nullptr;
    QLineEdit *m_name = nullptr;
    QLineEdit *m_number = nullptr;
    QComboBox *m_keepAwake = nullptr;
    QComboBox *m_preset = nullptr;
    QCheckBox *m_autoMirror = nullptr;
    QCheckBox *m_favorite = nullptr;
    QPlainTextEdit *m_notes = nullptr;
    QTableWidget *m_props = nullptr;
    QLineEdit *m_propFilter = nullptr;
    QTableWidget *m_apps = nullptr;
    QLineEdit *m_cmd = nullptr;
    QPlainTextEdit *m_out = nullptr;
    QListWidget *m_logs = nullptr;
};

} // namespace farm

#endif // FARM_UI_DEVICEINSPECTOR_H
