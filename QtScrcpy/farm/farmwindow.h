#ifndef FARM_FARMWINDOW_H
#define FARM_FARMWINDOW_H

#include <QHash>
#include <QList>
#include <QPoint>
#include <QSet>
#include <QStringList>
#include <QWidget>

#include "QtScrcpyCore.h"
#include "adbprocess.h"

class DeviceTile;
class FocusPanel;
class QGridLayout;
class QLabel;
class QLineEdit;
class QScrollArea;
class QResizeEvent;
class QMouseEvent;
class QWheelEvent;
class QKeyEvent;
class QPushButton;
class QVBoxLayout;
class QRubberBand;

/**
 * The v2.0 device-farm dashboard, styled after a control center: a left control
 * panel plus a scrollable grid of live DeviceTiles.
 *
 * Tiles keep a minimum width (the size slider has a floor) so phones stay
 * readable; the grid wraps into as many columns as fit and scrolls vertically
 * instead of shrinking tiles. Supports USB + WiFi/TCP connect, per-tile
 * mouse/keyboard control, a "Control All" group mode, and latency controls.
 */
class FarmWindow : public QWidget
{
    Q_OBJECT
public:
    explicit FarmWindow(QWidget *parent = nullptr);
    ~FarmWindow() override;

protected:
    void resizeEvent(QResizeEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private slots:
    void refreshDevices();
    void mirrorAll();
    void mirrorSelected();
    void selectAllDevices();
    void clearDeviceSelection();
    void createGroup();
    void stopAll();
    void connectWifi();
    void enableWifiSelected();
    void setTileSize(int width);
    void setHostSize(int height);
    void setQuality(int maxSize);
    void setFrameRate(int fps);
    void setGroupMode(bool on);
    void setSmallViewControl(bool on);

    void onDeviceConnected(bool success, const QString &serial, const QString &deviceName,
                           const QSize &size);
    void onDeviceDisconnected(const QString &serial);
    void onTileClicked(const QString &serial);
    void onTileDoubleClicked(const QString &serial);
    void onTileMouse(const QString &serial, QMouseEvent *event);
    void onTileWheel(const QString &serial, QWheelEvent *event);
    void onTileKey(const QString &serial, QKeyEvent *event);
    void onTileReloadRequested(const QString &serial);
    void onTileContextMenuRequested(const QString &serial, const QPoint &globalPos);

private:
    QWidget *buildControlPanel();
    QWidget *buildSelectorSection();
    void rebuildNumbering();
    void rebuildSelector();
    void updateSelectorStyles();
    void updateSelectorButtonStyle(const QString &serial, bool preview = false);
    void toggleSelection(const QString &serial);
    void updateTileSelectionStyles();
    void applyRubberSelection(const QRect &rect, bool additive);
    QString tileAt(const QPoint &point) const;
    QString selectorButtonAt(const QPoint &point) const;
    void updateHostTargets();
    void applyGroup(const QString &name);
    void loadGroups();
    void saveGroups();
    void rebuildGroups();
    void onAdbResult(qsc::AdbProcess::ADB_EXEC_RESULT result);
    DeviceTile *ensureTile(const QString &serial);
    void removeTile(const QString &serial);
    void relayout();
    void pumpConnectQueue();
    bool startConnect(const QString &serial);
    void updateConnectStatus();
    QList<QString> inputTargets(const QString &sourceSerial) const;
    void showTileContextMenu(const QString &serial, const QPoint &globalPos);
    void showMultiSelectContextMenu(const QPoint &globalPos);
    static QString serverPath();

    qsc::AdbProcess m_adb;
    QStringList m_available;
    QHash<QString, DeviceTile *> m_tiles;
    QList<QString> m_order;
    QStringList m_pending;             // serials waiting to connect
    QSet<QString> m_connecting;        // serials currently establishing
    QSet<QString> m_reloading;         // serials being reloaded (keep tile alive)
    QString m_selected;
    QString m_focusSerial;             // device shown in the embedded host panel
    QString m_wifiSerial;
    bool m_groupMode = false;
    bool m_smallViewControl = false;   // grid tiles control their device directly
    int m_portSeq = 0;                 // hands out a unique reverse port per device

    // Numbered selector + groups
    QSet<QString> m_selectedSerials;            // checked in the numbered selector
    QHash<QString, int> m_numbering;            // serial -> display number
    QHash<QString, QPushButton *> m_selectorButtons;
    QHash<QString, QStringList> m_groups;       // group name -> serials
    QGridLayout *m_selectorGrid = nullptr;
    QWidget *m_selectorGridHost = nullptr;
    QVBoxLayout *m_groupsLayout = nullptr;
    QRubberBand *m_selectorRubberBand = nullptr;
    QPoint m_selectorRubberOrigin;
    bool m_selectorDragging = false;
    QSet<QString> m_selectorPreDragSelection;
    QSet<QString> m_selectorCurrentDragSelection;
    QSet<QString> m_selectorButtonsInPreview;

    QGridLayout *m_grid = nullptr;
    QWidget *m_gridHost = nullptr;
    QScrollArea *m_scroll = nullptr;
    QRubberBand *m_rubberBand = nullptr;    // marquee visual (child of gridHost)
    QPoint m_rubberOrigin;
    bool m_dragging = false;
    QSet<QString> m_tilesUnderRubber;       // tiles currently under rubber band (for preview)
    FocusPanel *m_focusPanel = nullptr;
    QLabel *m_statusBar = nullptr;
    QLineEdit *m_ipEdit = nullptr;
    QLabel *m_tileSizeValue = nullptr;
    QLabel *m_hostSizeValue = nullptr;
    QLabel *m_qualityValue = nullptr;
    QLabel *m_fpsValue = nullptr;

    int m_tileWidth = 190;        // current grid tile width (slider, floor enforced)
    int m_hostHeight = 720;       // host panel target phone height (slider)

    // Low-latency DeviceParams template.
    quint16 m_maxSize = 800;
    quint32 m_bitRate = 4000000;
    quint32 m_maxFps = 30;
};

#endif // FARM_FARMWINDOW_H
