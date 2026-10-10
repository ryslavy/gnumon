#include "HotkeyPillWidget.h"
#include <QPainter>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QKeySequence>

namespace gnumon::gui {

HotkeyPillWidget::HotkeyPillWidget(const QString& hotkey, QWidget *parent)
    : QWidget(parent), hotkey_(hotkey)
{
    setFocusPolicy(Qt::StrongFocus);
    setCursor(Qt::PointingHandCursor);
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
}

void HotkeyPillWidget::setHotkey(const QString& hotkey) {
    if (hotkey_ != hotkey) {
        hotkey_ = hotkey;
        updateGeometry();
        update();
        emit hotkeyChanged(hotkey_);
    }
}

QSize HotkeyPillWidget::sizeHint() const {
    return QSize(220, 36);
}

void HotkeyPillWidget::paintEvent(QPaintEvent *) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    // Outer bounding box
    QRect r = rect().adjusted(1, 1, -1, -1);
    QColor bg(19, 21, 28);
    QColor border = recording_ ? QColor(33, 150, 243) : QColor(48, 52, 68);

    p.setBrush(bg);
    p.setPen(QPen(border, recording_ ? 1.5 : 1.0));
    p.drawRoundedRect(r, 4, 4);

    if (recording_) {
        p.setPen(QColor(33, 150, 243));
        QFont f = font();
        f.setPointSize(10);
        f.setBold(true);
        p.setFont(f);
        p.drawText(r, Qt::AlignCenter, "Press hotkey chord...");
        return;
    }

    if (hotkey_.trimmed().isEmpty()) {
        p.setPen(QColor(120, 125, 140));
        QFont f = font();
        f.setPointSize(10);
        p.setFont(f);
        p.drawText(r, Qt::AlignCenter, "Select hotkey chord");
        return;
    }

    // Split hotkey into individual parts (e.g. Ctrl, Shift, O)
    QStringList parts = hotkey_.split('+', Qt::SkipEmptyParts);
    for (auto &part : parts) part = part.trimmed();

    QFont pillFont = font();
    pillFont.setPointSize(9);
    pillFont.setBold(true);
    QFontMetrics fm(pillFont);

    int totalW = 0;
    for (int i = 0; i < parts.size(); ++i) {
        int pw = fm.horizontalAdvance(parts[i]) + 16;
        totalW += pw;
        if (i < parts.size() - 1) {
            totalW += 16; // spacing + plus
        }
    }

    int curX = std::max(r.left() + 8, r.center().x() - totalW / 2);
    int pillH = 22;
    int pillY = r.center().y() - pillH / 2;

    p.setFont(pillFont);

    for (int i = 0; i < parts.size(); ++i) {
        int pw = fm.horizontalAdvance(parts[i]) + 16;
        QRect pillRect(curX, pillY, pw, pillH);

        // Pill background & border
        p.setBrush(QColor(36, 40, 54));
        p.setPen(QPen(QColor(60, 66, 88), 1));
        p.drawRoundedRect(pillRect, 4, 4);

        // Pill text
        p.setPen(QColor(79, 195, 247));
        p.drawText(pillRect, Qt::AlignCenter, parts[i]);

        curX += pw;

        // Separator '+'
        if (i < parts.size() - 1) {
            QRect plusRect(curX, pillY, 16, pillH);
            p.setPen(QColor(140, 145, 160));
            p.drawText(plusRect, Qt::AlignCenter, "+");
            curX += 16;
        }
    }
}

void HotkeyPillWidget::mousePressEvent(QMouseEvent *event) {
    if (event->button() == Qt::LeftButton) {
        recording_ = true;
        setFocus();
        update();
        event->accept();
    } else {
        QWidget::mousePressEvent(event);
    }
}

void HotkeyPillWidget::keyPressEvent(QKeyEvent *event) {
    if (!recording_) {
        QWidget::keyPressEvent(event);
        return;
    }

    if (event->key() == Qt::Key_Escape) {
        recording_ = false;
        update();
        event->accept();
        return;
    }

    // Ignore solitary modifiers
    if (event->key() == Qt::Key_Control || event->key() == Qt::Key_Shift ||
        event->key() == Qt::Key_Alt || event->key() == Qt::Key_Meta) {
        QWidget::keyPressEvent(event);
        return;
    }

    // Build key string
    QStringList parts;
    if (event->modifiers() & Qt::ControlModifier) parts << "Ctrl";
    if (event->modifiers() & Qt::AltModifier) parts << "Alt";
    if (event->modifiers() & Qt::ShiftModifier) parts << "Shift";

    QString keyStr = QKeySequence(event->key()).toString();
    if (!keyStr.isEmpty()) {
        parts << keyStr;
    }

    if (!parts.isEmpty()) {
        setHotkey(parts.join("+"));
    }

    recording_ = false;
    update();
    event->accept();
}

void HotkeyPillWidget::focusOutEvent(QFocusEvent *event) {
    if (recording_) {
        recording_ = false;
        update();
    }
    QWidget::focusOutEvent(event);
}

} // namespace gnumon::gui
