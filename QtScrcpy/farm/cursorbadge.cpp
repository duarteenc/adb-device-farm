#include "cursorbadge.h"

#include <QFont>
#include <QFontMetrics>
#include <QPainter>
#include <QPaintEvent>

namespace {
constexpr int kPad = 9;        // left padding before the phone glyph
constexpr int kPhoneW = 11;    // phone glyph width
constexpr int kPhoneH = 18;    // phone glyph height
constexpr int kGap = 6;        // gap between glyph and number
constexpr int kRightPad = 11;  // padding after the number
constexpr int kHeight = 30;
}

CursorBadge::CursorBadge(QWidget *parent)
    : QWidget(parent)
{
    // Float above everything, never grab focus or input.
    setWindowFlags(Qt::ToolTip | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint
                   | Qt::WindowTransparentForInput);
    setAttribute(Qt::WA_TransparentForMouseEvents, true);
    setAttribute(Qt::WA_TranslucentBackground, true);
    setAttribute(Qt::WA_ShowWithoutActivating, true);
    setFocusPolicy(Qt::NoFocus);
    setCount(0);
}

void CursorBadge::setCount(int count)
{
    m_count = count;

    QFont f = font();
    f.setBold(true);
    f.setPointSize(11);
    const int textW = QFontMetrics(f).horizontalAdvance(QString::number(m_count));

    setFixedSize(kPad + kPhoneW + kGap + textW + kRightPad, kHeight);
    update();
}

void CursorBadge::moveToCursor(const QPoint &globalPos)
{
    // Sit just below-right of the cursor hotspot.
    move(globalPos + QPoint(14, 10));
}

void CursorBadge::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    // Rounded indigo chip.
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(0x63, 0x66, 0xf1));
    p.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), 9.0, 9.0);

    // Phone glyph (white rounded outline).
    const int py = (height() - kPhoneH) / 2;
    QRectF phone(kPad, py, kPhoneW, kPhoneH);
    QPen pen(QColor(0xff, 0xff, 0xff));
    pen.setWidthF(1.6);
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);
    p.drawRoundedRect(phone, 2.5, 2.5);

    // Number.
    QFont f = font();
    f.setBold(true);
    f.setPointSize(11);
    p.setFont(f);
    p.setPen(QColor(0xff, 0xff, 0xff));
    QRect textRect(kPad + kPhoneW + kGap, 0, width() - (kPad + kPhoneW + kGap), height());
    p.drawText(textRect, Qt::AlignVCenter | Qt::AlignLeft, QString::number(m_count));
}
