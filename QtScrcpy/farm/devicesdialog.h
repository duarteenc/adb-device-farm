#ifndef FARM_DEVICESDIALOG_H
#define FARM_DEVICESDIALOG_H

#include <QDialog>
#include <QHash>
#include <QSet>
#include <QStringList>

class QTableWidget;
class QLineEdit;
class QComboBox;
class QPushButton;
class QLabel;
class QCheckBox;

struct DeviceInfo {
    QString serial;
    QString model;
    QString ipPort;
    int index;
    bool connected;
};

/**
 * Devices dialog styled like GenFarmer — manage all ADB devices with advanced
 * filtering, bulk actions, and per-device controls.
 *
 * Features:
 * - Table view with Index, Phone name, Device ID columns
 * - Checkbox selection for bulk operations
 * - Search filter and Groups dropdown
 * - Total count display
 * - "Recargar" (refresh) and "Reiniciar ADB" buttons
 * - Per-row "Reiniciar" button and context menu
 * - Pagination (10/page default)
 * - Dark GenFarmer theme
 */
class DevicesDialog : public QDialog
{
    Q_OBJECT
public:
    explicit DevicesDialog(QWidget *parent = nullptr);
    ~DevicesDialog() override;

    void setDevices(const QList<DeviceInfo> &devices);
    QStringList selectedSerials() const;

signals:
    void refreshRequested();
    void restartAdbRequested();
    void restartDeviceRequested(const QString &serial);
    void connectDeviceRequested(const QString &serial);
    void disconnectDeviceRequested(const QString &serial);

private slots:
    void onRefresh();
    void onRestartAdb();
    void onRestartDevice(const QString &serial);
    void onSearchChanged(const QString &text);
    void onGroupChanged(int index);
    void onSelectAllToggled(bool checked);
    void onRowSelectionChanged();
    void onPageChanged(int page);
    void onPerPageChanged(const QString &text);
    void showDeviceContextMenu(const QPoint &pos);

private:
    void setupUi();
    void rebuildTable();
    void updatePagination();
    void updateTotalLabel();
    QList<DeviceInfo> filteredDevices() const;

    QList<DeviceInfo> m_devices;
    QSet<QString> m_selectedSerials;
    QString m_searchText;
    QString m_currentGroup;
    int m_currentPage = 1;
    int m_perPage = 10;

    QLabel *m_totalLabel = nullptr;
    QPushButton *m_refreshButton = nullptr;
    QPushButton *m_restartAdbButton = nullptr;
    QLineEdit *m_searchEdit = nullptr;
    QComboBox *m_groupsCombo = nullptr;
    QTableWidget *m_table = nullptr;
    QCheckBox *m_selectAllCheckbox = nullptr;
    QLabel *m_paginationLabel = nullptr;
    QPushButton *m_prevPageButton = nullptr;
    QPushButton *m_nextPageButton = nullptr;
    QComboBox *m_perPageCombo = nullptr;
    QHash<int, QString> m_rowToSerial;
};

#endif // FARM_DEVICESDIALOG_H
