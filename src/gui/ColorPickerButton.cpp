#include "ColorPickerButton.h"
#include <QVBoxLayout>
#include <QColorDialog>

namespace gnumon::gui {

ColorPickerButton::ColorPickerButton(const QString& label, const QColor& initialColor, QWidget *parent)
    : QWidget(parent), color_(initialColor)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(4);

    btn_ = new QPushButton(label, this);
    btn_->setStyleSheet(
        "QPushButton { background-color: #2b2e3c; color: #e0e0e5; border: 1px solid #3d4154; border-radius: 4px; padding: 6px 16px; font-weight: bold; }"
        "QPushButton:hover { background-color: #383c4e; border-color: #555b73; }"
    );
    connect(btn_, &QPushButton::clicked, this, &ColorPickerButton::OnButtonClicked);

    swatchStrip_ = new QWidget(this);
    swatchStrip_->setFixedHeight(8);
    swatchStrip_->setStyleSheet(QString("background-color: %1; border-radius: 2px;").arg(color_.name(QColor::HexArgb)));

    layout->addWidget(btn_);
    layout->addWidget(swatchStrip_);
}

void ColorPickerButton::setColor(const QColor& c) {
    if (color_ != c) {
        color_ = c;
        swatchStrip_->setStyleSheet(QString("background-color: %1; border-radius: 2px;").arg(color_.name(QColor::HexArgb)));
        emit colorChanged(color_);
    }
}

void ColorPickerButton::OnButtonClicked() {
    QColor chosen = QColorDialog::getColor(color_, this, "Select Color", QColorDialog::ShowAlphaChannel);
    if (chosen.isValid()) {
        setColor(chosen);
    }
}

} // namespace gnumon::gui
