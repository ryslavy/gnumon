#include "QuadrantPositioner.h"
#include <QPainter>
#include <QMouseEvent>

namespace gnumon::gui {

QuadrantPositioner::QuadrantPositioner(QWidget *parent)
    : QWidget(parent)
{
    setCursor(Qt::PointingHandCursor);
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
}

void QuadrantPositioner::setPosition(int pos) {
    if (position_ != pos && pos >= 0 && pos <= 3) {
        position_ = pos;
        update();
        emit positionChanged(position_);
    }
}

QSize QuadrantPositioner::sizeHint() const {
    return QSize(100, 80);
}

void QuadrantPositioner::paintEvent(QPaintEvent *) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    QRect r = rect().adjusted(1, 1, -1, -1);
    int midX = r.center().x();
    int midY = r.center().y();

    QRect tl(r.left(), r.top(), midX - r.left(), midY - r.top());
    QRect tr(midX, r.top(), r.right() - midX, midY - r.top());
    QRect bl(r.left(), midY, midX - r.left(), r.bottom() - midY);
    QRect br(midX, midY, r.right() - midX, r.bottom() - midY);

    QRect quads[4] = {tl, tr, bl, br};

    QColor normalColor(70, 74, 86);
    QColor activeColor(144, 202, 249); // light blue matching Overlay-Config-menu.png

    for (int i = 0; i < 4; ++i) {
        p.setBrush(i == position_ ? activeColor : normalColor);
        p.setPen(QPen(QColor(25, 27, 34), 2));
        p.drawRect(quads[i]);
    }

    // Outer border
    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(QColor(40, 44, 56), 1.5));
    p.drawRect(r);
}

void QuadrantPositioner::mousePressEvent(QMouseEvent *event) {
    if (event->button() == Qt::LeftButton) {
        int midX = width() / 2;
        int midY = height() / 2;

        int newPos = 0;
        if (event->position().x() < midX && event->position().y() < midY) newPos = 0; // TL
        else if (event->position().x() >= midX && event->position().y() < midY) newPos = 1; // TR
        else if (event->position().x() < midX && event->position().y() >= midY) newPos = 2; // BL
        else newPos = 3; // BR

        setPosition(newPos);
        event->accept();
    } else {
        QWidget::mousePressEvent(event);
    }
}

} // namespace gnumon::gui
