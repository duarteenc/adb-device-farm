#ifndef FARM_CURSORBADGE_H
#define FARM_CURSORBADGE_H

#include <QWidget>

/**
 * A small frameless chip that floats just off the cursor while the user drags a
 * multi-selection on the farm grid (GenFarmer-style): a phone glyph plus the
 * number of currently selected devices. Purely a visual carry indicator — it is
 * transparent to input and never steals focus.
 */
class CursorBadge : public QWidget
{
    Q_OBJECT
public:
    explicit CursorBadge(QWidget *parent = nullptr);

    void setCount(int count);
    void moveToCursor(const QPoint &globalPos);

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    int m_count = 0;
};

#endif // FARM_CURSORBADGE_H
