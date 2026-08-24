#ifndef FARM_UI_DEVICESPAGE_H
#define FARM_UI_DEVICESPAGE_H

#include <QHash>
#include <QSet>
#include <QStringList>
#include <QTimer>
#include <QWidget>

#include "devices/devicerecord.h"

class QComboBox;
class QLabel;
class QLineEdit;
class QMenu;
class QPushButton;
class QSlider;
class QCheckBox;
class QTableWidget;
class QStackedWidget;
class QListWidget;
class CursorBadge;    // global (ui/cursorbadge.h)

namespace farm {

class DeviceGrid;
class DeviceTile;
class FocusPanel;

/**
 * The Devices page: live grid (or list) of every known device, selection tools,
 * quality presets, host/master panel and every batch action operators use most
 * (mirror/stop, screenshot, text, APK, files, keep-awake, groups, reboot…).
 */
class DevicesPage : public QWidget
{
    Q_OBJECT
public:
    explicit DevicesPage(QWidget *parent = nullptr);
    ~DevicesPage() override;

    QStringList selectedIds() const;
    void selectIds(const QStringList &ids);
    void setSearchQuery(const QString &query);
    void openHost(const QString &id);
    void focusDevice(const QString &id);    // scroll + highlight
    void setGroupFilter(const QString &group);

signals:
    void consoleRequested(const QStringList &ids);
    void appsRequested(const QStringList &ids);
    void filesRequested(const QStringList &ids);
    void inspectRequested(const QString &id);
    void automationRequested(const QStringList &ids);
    void statusMessage(const QString &text);
    void selectionChanged(int count);

private:
    QWidget *buildToolbar();
    QWidget *buildSidePanel();
    void buildListView();
    void refreshOrder();
    void refreshCounts();
    void refreshListRow(const QString &id);
    void refreshGroupsList();
    void onDeviceAdded(const QString &id);
    void onDeviceChanged(const QString &id);
    void onDeviceRemoved(const QString &id);
    void onMirrorStarted(const QString &id, const QSize &size);
    void onMirrorStopped(const QString &id);
    void attachTile(const QString &id);
    void detachTile(const QString &id);
    void showContextMenu(const QString &id, const QPoint &globalPos);
    void buildGroupMenu(QMenu *menu, const QStringList &ids);
    QStringList targetsFor(const QString &clicked) const;
    bool confirmBulk(const QString &action, int count, bool destructive);
    void onTileDoubleClicked(const QString &id);
    void closeHost();
    void updateHostFollowers();
    void onTileMouse(const QString &id, QMouseEvent *event);
    void onTileWheel(const QString &id, QWheelEvent *event);
    void onTileKey(const QString &id, QKeyEvent *event);
    QStringList inputTargets(const QString &source) const;
    void applyPreset(const QString &name);
    void applyCustomProfile();
    void connectWifiRange();
    void enableWifiOnSelected();
    void setNumberedWallpapers(const QStringList &ids);
    void ensureHelperApk(const QString &id);

    // ---- actions ----
    void actMirror(const QStringList &ids);
    void actStop(const QStringList &ids);
    void actRestart(const QStringList &ids);
    void actReconnect(const QStringList &ids);
    void actReboot(const QStringList &ids);
    void actScreenshot(const QStringList &ids);
    void actRecord(const QStringList &ids);
    void actSendText(const QStringList &ids);
    void actInstallApk(const QStringList &ids, const QString &path = QString());
    void actUploadFile(const QStringList &ids, const QStringList &paths = QStringList());
    void actLaunchApp(const QStringList &ids);
    void actKeepAwake(const QStringList &ids, bool apply);
    void actWake(const QStringList &ids);
    void actRename(const QString &id);
    void actSetNumber(const QStringList &ids);
    void actFavorite(const QStringList &ids, bool on);
    void actDisconnect(const QStringList &ids);
    void actForget(const QStringList &ids);
    void actNormalizeReset(const QStringList &ids);

    DeviceGrid *m_grid = nullptr;
    FocusPanel *m_focus = nullptr;
    QStackedWidget *m_viewStack = nullptr;
    QTableWidget *m_list = nullptr;
    ::CursorBadge *m_badge = nullptr;
    QTimer m_badgeTimer;
    QTimer m_orderTimer;
    QString m_focusId;
    QString m_search;
    QString m_groupFilter;
    QString m_filter = QStringLiteral("all");
    QString m_sortKey = QStringLiteral("number");
    bool m_controlAll = false;
    bool m_smallViewControl = false;
    QSet<QString> m_helperChecked;
    QHash<QString, int> m_listRows;

    // widgets
    QLineEdit *m_searchEdit = nullptr;
    QComboBox *m_filterCombo = nullptr;
    QComboBox *m_sortCombo = nullptr;
    QComboBox *m_densityCombo = nullptr;
    QPushButton *m_viewToggle = nullptr;
    QLabel *m_countLabel = nullptr;
    QLabel *m_selectionLabel = nullptr;
    QLabel *m_statusLabel = nullptr;
    QComboBox *m_presetCombo = nullptr;
    QSlider *m_sizeSlider = nullptr;
    QSlider *m_fpsSlider = nullptr;
    QSlider *m_bitrateSlider = nullptr;
    QLabel *m_sizeValue = nullptr;
    QLabel *m_fpsValue = nullptr;
    QLabel *m_bitrateValue = nullptr;
    QSlider *m_tileSlider = nullptr;
    QSlider *m_hostSlider = nullptr;
    QCheckBox *m_controlAllCheck = nullptr;
    QCheckBox *m_smallCtrlCheck = nullptr;
    QListWidget *m_groupsList = nullptr;
    QLineEdit *m_wifiRange = nullptr;
    QLineEdit *m_wifiPort = nullptr;
};

} // namespace farm

#endif // FARM_UI_DEVICESPAGE_H
