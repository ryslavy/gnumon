#pragma once

#include <QWidget>
#include <QColor>
#include <QPushButton>

namespace gnumon::gui {

class ColorPickerButton : public QWidget {
    Q_OBJECT

public:
    explicit ColorPickerButton(const QString& label = "Background", const QColor& initialColor = QColor(50, 57, 91, 220), QWidget *parent = nullptr);

    QColor color() const { return color_; }
    void setColor(const QColor& c);

signals:
    void colorChanged(const QColor& c);

private slots:
    void OnButtonClicked();

private:
    QColor color_;
    QPushButton *btn_ = nullptr;
    QWidget *swatchStrip_ = nullptr;
};

} // namespace gnumon::gui
