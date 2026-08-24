#include "farmtheme.h"

#include <QFrame>
#include <QLabel>
#include <QPushButton>
#include <QWidget>

#include "core/farmsettings.h"
#include "devices/devicerecord.h"

namespace farm {
namespace theme {

namespace {
struct Palette
{
    const char *bg;
    const char *panel;
    const char *panel2;
    const char *border;
    const char *text;
    const char *muted;
    const char *accent;
    const char *accentHover;
    const char *input;
    const char *hover;
    const char *selected;
};
const Palette kDark = { "#0b0f17", "#121826", "#161e2e", "#243048", "#e2e8f0", "#7c8aa0", "#2563eb", "#3b82f6", "#0f1422", "#1c2536", "#1e3a5f" };
const Palette kLight = { "#f3f5f9", "#ffffff", "#f7f9fc", "#d5dbe5", "#0f172a", "#64748b", "#2563eb", "#1d4ed8", "#ffffff", "#e8edf5", "#dbe7ff" };

const Palette &palette()
{
    return isDark() ? kDark : kLight;
}
} // namespace

bool isDark()
{
    return FarmSettings::instance().stringValue(QStringLiteral("general/theme"), QStringLiteral("dark")) != QLatin1String("light");
}

QColor background() { return QColor(QLatin1String(palette().bg)); }
QColor panel() { return QColor(QLatin1String(palette().panel)); }
QColor border() { return QColor(QLatin1String(palette().border)); }
QColor text() { return QColor(QLatin1String(palette().text)); }
QColor textMuted() { return QColor(QLatin1String(palette().muted)); }
QColor accent() { return QColor(QLatin1String(palette().accent)); }
QColor success() { return QColor(0x22, 0xc5, 0x5e); }
QColor warning() { return QColor(0xf5, 0x9e, 0x0b); }
QColor danger() { return QColor(0xef, 0x44, 0x44); }
QColor purple() { return QColor(0xa8, 0x55, 0xf7); }

QColor stateColor(int deviceState)
{
    switch (static_cast<DeviceState>(deviceState)) {
    case DeviceState::Mirroring:
        return success();
    case DeviceState::AdbOnline:
        return QColor(0x38, 0xbd, 0xf8);
    case DeviceState::Busy:
        return purple();
    case DeviceState::Connecting:
    case DeviceState::Reconnecting:
        return warning();
    case DeviceState::Unauthorized:
        return QColor(0xfb, 0x92, 0x3c);
    case DeviceState::Error:
        return danger();
    case DeviceState::Discovered:
        return QColor(0x94, 0xa3, 0xb8);
    case DeviceState::Offline:
    case DeviceState::Unknown:
        break;
    }
    return QColor(0x47, 0x55, 0x69);
}

QString stateGlyph(int deviceState)
{
    switch (static_cast<DeviceState>(deviceState)) {
    case DeviceState::Mirroring:
        return QString(QChar(0x25CF));    // ●
    case DeviceState::AdbOnline:
        return QString(QChar(0x25CB));    // ○
    case DeviceState::Busy:
        return QString(QChar(0x2699));    // ⚙
    case DeviceState::Connecting:
    case DeviceState::Reconnecting:
        return QString(QChar(0x21BB));    // ↻
    case DeviceState::Unauthorized:
        return QString(QChar(0x26A0));    // ⚠
    case DeviceState::Error:
        return QString(QChar(0x2716));    // ✖
    case DeviceState::Discovered:
        return QString(QChar(0x25CC));    // ◌
    default:
        return QString(QChar(0x25CB));
    }
}

QString stylesheet(bool dark)
{
    const Palette &p = dark ? kDark : kLight;
    QString css = QStringLiteral(R"(
QMainWindow, QDialog, #farmRoot { background:%BG%; }
QWidget { color:%TEXT%; font-size:12px; }
QWidget#page { background:%BG%; }
#navBar { background:%PANEL%; border-right:1px solid %BORDER%; }
#navBar QListWidget { background:transparent; border:none; outline:none; font-size:13px; }
#navBar QListWidget::item { padding:9px 14px; border-radius:6px; margin:1px 8px; }
#navBar QListWidget::item:hover { background:%HOVER%; }
#navBar QListWidget::item:selected { background:%SELECTED%; color:%TEXT%; }
#topBar { background:%PANEL%; border-bottom:1px solid %BORDER%; }
#sidePanel, #card { background:%PANEL%; border:1px solid %BORDER%; border-radius:8px; }
#sidePanel QLabel { background:transparent; }
QLabel#hint { color:%MUTED%; }
QLabel#sectionTitle { font-weight:bold; font-size:12px; color:%MUTED%; padding-top:6px; letter-spacing:0.5px; }
QLabel#pageTitle { font-size:20px; font-weight:bold; }
QLabel#statValue { font-size:26px; font-weight:bold; }
QLabel#statLabel { color:%MUTED%; font-size:11px; }
QPushButton { background:%PANEL2%; border:1px solid %BORDER%; border-radius:6px; padding:6px 12px; }
QPushButton:hover { background:%HOVER%; }
QPushButton:pressed { background:%SELECTED%; }
QPushButton:disabled { color:%MUTED%; }
QPushButton#primary { background:%ACCENT%; border:none; color:white; font-weight:bold; }
QPushButton#primary:hover { background:%ACCENTH%; }
QPushButton#danger { background:#7f1d1d; border:none; color:white; }
QPushButton#danger:hover { background:#991b1b; }
QPushButton#quiet { background:transparent; border:none; padding:4px 6px; color:%MUTED%; }
QPushButton#quiet:hover { color:%TEXT%; background:%HOVER%; }
QPushButton#iconButton { background:transparent; border:none; padding:0; margin:0; color:%MUTED%; font-size:14px; }
QPushButton#iconButton:hover { color:%TEXT%; }
QToolButton { background:%PANEL2%; border:1px solid %BORDER%; border-radius:6px; padding:4px 8px; }
QToolButton:hover { background:%HOVER%; }
QLineEdit, QTextEdit, QPlainTextEdit, QSpinBox, QDoubleSpinBox, QComboBox, QDateTimeEdit, QTimeEdit {
    background:%INPUT%; border:1px solid %BORDER%; border-radius:5px; padding:5px 7px; selection-background-color:%ACCENT%; }
QLineEdit:focus, QTextEdit:focus, QPlainTextEdit:focus, QComboBox:focus, QSpinBox:focus { border:1px solid %ACCENTH%; }
QComboBox::drop-down { border:none; width:22px; }
QComboBox QAbstractItemView { background:%PANEL%; border:1px solid %BORDER%; selection-background-color:%SELECTED%; }
QCheckBox { spacing:6px; }
QCheckBox::indicator { width:15px; height:15px; border:1px solid %BORDER%; border-radius:3px; background:%INPUT%; }
QCheckBox::indicator:checked { background:%ACCENT%; border-color:%ACCENT%; }
QCheckBox::indicator:indeterminate { background:%ACCENTH%; }
QScrollArea { background:transparent; border:none; }
QScrollBar:vertical { background:transparent; width:10px; margin:0; }
QScrollBar::handle:vertical { background:%BORDER%; border-radius:5px; min-height:24px; }
QScrollBar::handle:vertical:hover { background:%MUTED%; }
QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height:0; }
QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { background:none; }
QScrollBar:horizontal { background:transparent; height:10px; margin:0; }
QScrollBar::handle:horizontal { background:%BORDER%; border-radius:5px; min-width:24px; }
QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal { width:0; }
QSlider::groove:horizontal { height:4px; background:%BORDER%; border-radius:2px; }
QSlider::handle:horizontal { width:14px; margin:-6px 0; border-radius:7px; background:%ACCENTH%; }
QSlider::sub-page:horizontal { background:%ACCENT%; border-radius:2px; }
QTableView, QTreeView, QListView, QTableWidget, QTreeWidget, QListWidget { background:%PANEL%; border:1px solid %BORDER%; border-radius:6px; gridline-color:%BORDER%; alternate-background-color:%PANEL2%; outline:none; }
QTableView::item, QTreeView::item { padding:3px 6px; }
QTableView::item:selected, QTreeView::item:selected, QListView::item:selected { background:%SELECTED%; color:%TEXT%; }
QHeaderView::section { background:%PANEL2%; color:%MUTED%; border:none; border-bottom:1px solid %BORDER%; border-right:1px solid %BORDER%; padding:5px 6px; font-weight:bold; }
QTabWidget::pane { border:1px solid %BORDER%; border-radius:6px; background:%PANEL%; }
QTabBar::tab { background:%PANEL2%; color:%MUTED%; padding:7px 14px; border:1px solid %BORDER%; border-bottom:none; border-top-left-radius:6px; border-top-right-radius:6px; margin-right:2px; }
QTabBar::tab:selected { background:%PANEL%; color:%TEXT%; }
QTabBar::tab:hover { color:%TEXT%; }
QMenu { background:%PANEL%; border:1px solid %BORDER%; padding:4px; }
QMenu::item { padding:6px 24px 6px 12px; border-radius:4px; }
QMenu::item:selected { background:%SELECTED%; }
QMenu::separator { height:1px; background:%BORDER%; margin:4px 8px; }
QToolTip { background:%PANEL%; color:%TEXT%; border:1px solid %BORDER%; padding:4px; }
QProgressBar { background:%INPUT%; border:1px solid %BORDER%; border-radius:5px; text-align:center; height:16px; }
QProgressBar::chunk { background:%ACCENT%; border-radius:4px; }
QGroupBox { border:1px solid %BORDER%; border-radius:6px; margin-top:16px; padding-top:6px; }
QGroupBox::title { subcontrol-origin:margin; left:10px; padding:0 4px; color:%MUTED%; }
QStatusBar { background:%PANEL%; border-top:1px solid %BORDER%; }
QSplitter::handle { background:%BORDER%; }
QSplitter::handle:horizontal { width:2px; }
QSplitter::handle:vertical { height:2px; }
#tileOverlay { background:transparent; }
#tileNum { color:#ffffff; font-size:15px; font-weight:bold; }
#tileName { color:#f1f5f9; font-size:11px; font-weight:bold; }
#tileIp { color:#ffffff; font-size:10px; }
#tileMeta { color:#dbe4f0; font-size:9px; }
#tileNum[sel="true"], #tileName[sel="true"], #tileIp[sel="true"] { color:#7cc0ff; }
)");
    css.replace(QLatin1String("%BG%"), QLatin1String(p.bg));
    css.replace(QLatin1String("%PANEL2%"), QLatin1String(p.panel2));
    css.replace(QLatin1String("%PANEL%"), QLatin1String(p.panel));
    css.replace(QLatin1String("%BORDER%"), QLatin1String(p.border));
    css.replace(QLatin1String("%TEXT%"), QLatin1String(p.text));
    css.replace(QLatin1String("%MUTED%"), QLatin1String(p.muted));
    css.replace(QLatin1String("%ACCENTH%"), QLatin1String(p.accentHover));
    css.replace(QLatin1String("%ACCENT%"), QLatin1String(p.accent));
    css.replace(QLatin1String("%INPUT%"), QLatin1String(p.input));
    css.replace(QLatin1String("%HOVER%"), QLatin1String(p.hover));
    css.replace(QLatin1String("%SELECTED%"), QLatin1String(p.selected));
    return css;
}

void apply(QWidget *root)
{
    root->setStyleSheet(stylesheet(isDark()));
}

QPushButton *button(const QString &text, QWidget *parent, const QString &role)
{
    auto *b = new QPushButton(text, parent);
    if (!role.isEmpty()) {
        b->setObjectName(role);
    }
    b->setCursor(Qt::PointingHandCursor);
    return b;
}

QPushButton *iconButton(const QString &glyph, const QString &tooltip, QWidget *parent)
{
    auto *b = new QPushButton(glyph, parent);
    b->setObjectName(QStringLiteral("iconButton"));
    b->setToolTip(tooltip);
    b->setFixedSize(24, 22);
    b->setCursor(Qt::PointingHandCursor);
    return b;
}

QLabel *hint(const QString &text, QWidget *parent)
{
    auto *l = new QLabel(text, parent);
    l->setObjectName(QStringLiteral("hint"));
    l->setWordWrap(true);
    return l;
}

QLabel *sectionTitle(const QString &text, QWidget *parent)
{
    auto *l = new QLabel(text.toUpper(), parent);
    l->setObjectName(QStringLiteral("sectionTitle"));
    return l;
}

QWidget *separator(QWidget *parent)
{
    auto *line = new QFrame(parent);
    line->setFrameShape(QFrame::HLine);
    line->setFixedHeight(1);
    line->setStyleSheet(QStringLiteral("background:%1; border:none;").arg(border().name()));
    return line;
}

} // namespace theme
} // namespace farm
