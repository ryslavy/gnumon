#pragma once

#include <QWidget>
#include <QString>
#include <QPropertyAnimation>

namespace gnumon::gui {

class ToggleSwitch : public QWidget {
    Q_OBJECT
    Q_PROPERTY(qreal offset READ offset WRITE setOffset)

public:
    explicit ToggleSwitch(const QString& text = "", QWidget *parent = nullptr);

    bool isChecked() const { return checked_; }
    void setChecked(bool checked);

    QString text() const { return text_; }
    void setText(const QString& text);

    qreal offset() const { return offset_; }
    void setOffset(qreal o);

    QSize sizeHint() const override;

signals:
    void toggled(bool checked);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;

private:
    bool checked_ = false;
    QString text_;
    qreal offset_ = 0.0;
    QPropertyAnimation *animation_ = nullptr;
};

} // namespace gnumon::gui
