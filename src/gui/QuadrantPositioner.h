#pragma once

#include <QWidget>

namespace gnumon::gui {

class QuadrantPositioner : public QWidget {
    Q_OBJECT

public:
    explicit QuadrantPositioner(QWidget *parent = nullptr);

    int position() const { return position_; }
    void setPosition(int pos);

    QSize sizeHint() const override;

signals:
    void positionChanged(int pos);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;

private:
    int position_ = 0; // 0: Top-Left, 1: Top-Right, 2: Bottom-Left, 3: Bottom-Right
};

} // namespace gnumon::gui
