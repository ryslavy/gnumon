#include "ToggleSwitch.h"
#include <QPainter>
#include <QMouseEvent>
#include <QKeyEvent>

namespace gnumon::gui {

ToggleSwitch::ToggleSwitch(const QString& text, QWidget *parent)
    : QWidget(parent), text_(text)
{
    setFocusPolicy(Qt::StrongFocus);
    setCursor(Qt::PointingHandCursor);

    animation_ = new QPropertyAnimation(this, "offset", this);
    animation_->setDuration(120);
}

void ToggleSwitch::setChecked(bool checked) {
    if (checked_ == checked) return;
    checked_ = checked;

    animation_->stop();
    animation_->setStartValue(offset_);
    animation_->setEndValue(checked_ ? 1.0 : 0.0);
    animation_->start();

    emit toggled(checked_);
    update();
}

void ToggleSwitch::setText(const QString& text) {
    text_ = text;
    updateGeometry();
    update();
}

void ToggleSwitch::setOffset(qreal o) {
    offset_ = o;
    update();
}

QSize ToggleSwitch::sizeHint() const {
    int switchWidth = 42;
    int switchHeight = 22;
    if (text_.isEmpty()) {
        return QSize(switchWidth + 4, switchHeight + 4);
    }
    QFontMetrics fm(font());
    int textW = fm.horizontalAdvance(text_);
    return QSize(switchWidth + 8 + textW + 4, std::max(switchHeight + 4, fm.height() + 4));
}

void ToggleSwitch::paintEvent(QPaintEvent *) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    int trackW = 40;
    int trackH = 20;
    int trackX = 2;
    int trackY = (height() - trackH) / 2;

    // Background track colors
    QColor trackOff(72, 75, 88);
    QColor trackOn(25, 118, 210); // Intel blue

    int r = static_cast<int>(trackOff.red() + offset_ * (trackOn.red() - trackOff.red()));
    int g = static_cast<int>(trackOff.green() + offset_ * (trackOn.green() - trackOff.green()));
    int b = static_cast<int>(trackOff.blue() + offset_ * (trackOn.blue() - trackOff.blue()));
    QColor curTrack(r, g, b);

    p.setPen(Qt::NoPen);
    p.setBrush(curTrack);
    p.drawRoundedRect(trackX, trackY, trackW, trackH, trackH / 2.0, trackH / 2.0);

    // Thumb circle
    int thumbD = 14;
    int thumbMinX = trackX + 3;
    int thumbMaxX = trackX + trackW - thumbD - 3;
    qreal thumbX = thumbMinX + offset_ * (thumbMaxX - thumbMinX);
    qreal thumbY = trackY + (trackH - thumbD) / 2.0;

    p.setBrush(Qt::white);
    p.drawEllipse(QRectF(thumbX, thumbY, thumbD, thumbD));

    // Optional text on right
    if (!text_.isEmpty()) {
        p.setPen(QColor(230, 230, 235));
        QFont f = font();
        f.setPointSize(10);
        p.setFont(f);

        int textX = trackX + trackW + 8;
        QRect textRect(textX, 0, width() - textX, height());
        p.drawText(textRect, Qt::AlignVCenter | Qt::AlignLeft, text_);
    }
}

void ToggleSwitch::mousePressEvent(QMouseEvent *event) {
    if (event->button() == Qt::LeftButton) {
        event->accept();
    } else {
        QWidget::mousePressEvent(event);
    }
}

void ToggleSwitch::mouseReleaseEvent(QMouseEvent *event) {
    if (event->button() == Qt::LeftButton) {
        if (rect().contains(event->pos())) {
            setChecked(!checked_);
        }
        event->accept();
    } else {
        QWidget::mouseReleaseEvent(event);
    }
}

void ToggleSwitch::keyPressEvent(QKeyEvent *event) {
    if (event->key() == Qt::Key_Space || event->key() == Qt::Key_Return) {
        setChecked(!checked_);
        event->accept();
    } else {
        QWidget::keyPressEvent(event);
    }
}

} // namespace gnumon::gui
