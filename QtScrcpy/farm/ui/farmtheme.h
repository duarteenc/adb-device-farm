#ifndef FARM_UI_FARMTHEME_H
#define FARM_UI_FARMTHEME_H

#include <QColor>
#include <QString>

class QWidget;
class QPushButton;
class QLabel;

namespace farm {

/**
 * Colours, stylesheet and small widget factories shared by every page so the
 * product looks like one application (dark by default; a light variant is
 * selectable in Settings › General).
 */
namespace theme {

QString stylesheet(bool dark);
bool isDark();
void apply(QWidget *root);

QColor background();
QColor panel();
QColor border();
QColor text();
QColor textMuted();
QColor accent();
QColor success();
QColor warning();
QColor danger();
QColor purple();

QColor stateColor(int deviceState);    // farm::DeviceState as int to avoid a header dependency here
QString stateGlyph(int deviceState);

QPushButton *button(const QString &text, QWidget *parent, const QString &role = QString());    // role: primary/danger/quiet
QPushButton *iconButton(const QString &glyph, const QString &tooltip, QWidget *parent);
QLabel *hint(const QString &text, QWidget *parent);
QLabel *sectionTitle(const QString &text, QWidget *parent);
QWidget *separator(QWidget *parent);

} // namespace theme
} // namespace farm

#endif // FARM_UI_FARMTHEME_H
