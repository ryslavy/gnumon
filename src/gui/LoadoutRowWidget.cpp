#include "LoadoutRowWidget.h"
#include <QHBoxLayout>
#include <QColorDialog>

namespace gnumon::gui {

struct MetricDef {
    int id;
    const char* name;
    bool isNumeric;
};

static const MetricDef kMetrics[] = {
    {3, "GPU Name", false},
    {5, "CPU Name", false},
    {12, "FPS-Presents", true},
    {11, "FPS-Displayed", true},
    {87, "FrameTime-Presents", true},
    {8, "FrameTime-App", true},
    {13, "Ms GPU Time", true},
    {14, "Ms GPU Busy", true},
    {15, "Ms GPU Wait", true},
    {33, "GPU Utilization", true},
    {28, "GPU Power", true},
    {31, "GPU Temperature", true},
    {30, "GPU Frequency", true},
    {29, "GPU Voltage", true},
    {47, "GPU VRAM", true},
    {34, "CPU Utilization", true},
    {89, "CPU Power", true},
    {90, "CPU Temperature", true},
    {91, "CPU Frequency", true},
    {9, "CPU Busy", true},
    {10, "CPU Wait", true},
    {25, "Display Latency", true},
    {26, "Click to Photon Latency", true},
    {27, "Ms Animation Error", true}
};

LoadoutRowWidget::LoadoutRowWidget(const LoadoutWidget& widget, int index, QWidget *parent)
    : QWidget(parent), widget_(widget), index_(index)
{
    if (widget_.metrics.isEmpty()) {
        widget_.metrics.append(LoadoutMetricItem());
    }
    SetupUi();
}

void LoadoutRowWidget::SetupUi() {
    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(6, 4, 6, 4);
    layout->setSpacing(8);

    setStyleSheet(
        "LoadoutRowWidget {"
        "  background-color: #1a1b24;"
        "  border: 1px solid #282937;"
        "  border-radius: 4px;"
        "}"
        "QComboBox {"
        "  background-color: #12131a;"
        "  border: 1px solid #363948;"
        "  border-radius: 3px;"
        "  padding: 4px 8px;"
        "  color: #e0e0e5;"
        "  min-height: 24px;"
        "}"
        "QComboBox:disabled {"
        "  color: #555866;"
        "  background-color: #181920;"
        "}"
        "QPushButton {"
        "  background-color: transparent;"
        "  color: #a0a4b8;"
        "  border: none;"
        "  font-size: 14px;"
        "  padding: 4px;"
        "}"
        "QPushButton:hover {"
        "  color: #ffffff;"
        "}"
    );

    // Grip icon
    lblGrip_ = new QLabel("≡", this);
    lblGrip_->setStyleSheet("color: #70758a; font-size: 18px; font-weight: bold; padding: 0 4px;");
    lblGrip_->setFixedWidth(20);
    layout->addWidget(lblGrip_);

    // Metric Combobox
    comboMetric_ = new QComboBox(this);
    int selectedMetricIdx = 0;
    for (size_t i = 0; i < sizeof(kMetrics)/sizeof(kMetrics[0]); ++i) {
        comboMetric_->addItem(kMetrics[i].name, kMetrics[i].id);
        if (kMetrics[i].id == widget_.metrics[0].metricId) {
            selectedMetricIdx = static_cast<int>(i);
        }
    }
    comboMetric_->setCurrentIndex(selectedMetricIdx);
    comboMetric_->setMinimumWidth(180);
    connect(comboMetric_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &LoadoutRowWidget::OnMetricChanged);
    layout->addWidget(comboMetric_, 3);

    // Stat Combobox
    comboStat_ = new QComboBox(this);
    comboStat_->addItem("None", 0);
    comboStat_->addItem("avg", 1);
    comboStat_->addItem("1%", 5);
    comboStat_->addItem("99%", 6);
    comboStat_->addItem("raw", 4);
    comboStat_->addItem("min", 2);
    comboStat_->addItem("max", 3);
    
    int statIdx = comboStat_->findData(widget_.metrics[0].statId);
    comboStat_->setCurrentIndex(statIdx >= 0 ? statIdx : 0);
    comboStat_->setMinimumWidth(80);
    connect(comboStat_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &LoadoutRowWidget::OnStatChanged);
    layout->addWidget(comboStat_, 1);

    // Widget Type Combobox
    comboType_ = new QComboBox(this);
    comboType_->addItem("Readout", static_cast<int>(WidgetType::Readout));
    comboType_->addItem("Graph", static_cast<int>(WidgetType::Graph));
    comboType_->setCurrentIndex(widget_.widgetType == WidgetType::Graph ? 1 : 0);
    comboType_->setMinimumWidth(90);
    connect(comboType_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &LoadoutRowWidget::OnTypeChanged);
    layout->addWidget(comboType_, 1);

    // Subtype Combobox (Line / Histogram)
    comboSubtype_ = new QComboBox(this);
    comboSubtype_->addItem("Line", static_cast<int>(GraphType::Line));
    comboSubtype_->addItem("Histogram", static_cast<int>(GraphType::Histogram));
    comboSubtype_->setCurrentIndex(widget_.graphType == GraphType::Histogram ? 1 : 0);
    comboSubtype_->setEnabled(widget_.widgetType == WidgetType::Graph);
    comboSubtype_->setMinimumWidth(90);
    connect(comboSubtype_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &LoadoutRowWidget::OnSubtypeChanged);
    layout->addWidget(comboSubtype_, 1);

    // Color Swatch Button
    btnColor_ = new QPushButton(this);
    btnColor_->setFixedSize(28, 22);
    QColor col = widget_.metrics[0].lineColor;
    btnColor_->setStyleSheet(QString("background-color: %1; border: 1px solid #4fc3f7; border-radius: 3px;").arg(col.name()));
    btnColor_->setVisible(widget_.widgetType == WidgetType::Graph);
    connect(btnColor_, &QPushButton::clicked, this, &LoadoutRowWidget::OnPickColor);
    layout->addWidget(btnColor_);

    // Cog icon (details)
    btnDetails_ = new QPushButton("⚙", this);
    btnDetails_->setToolTip("Widget Details & Thresholds");
    layout->addWidget(btnDetails_);

    // Duplicate / Add Line (+)
    btnAdd_ = new QPushButton("+", this);
    btnAdd_->setToolTip("Add Line or Duplicate Widget");
    connect(btnAdd_, &QPushButton::clicked, this, [this]() {
        emit duplicateRequested(index_);
    });
    layout->addWidget(btnAdd_);

    // Delete (✕)
    btnDelete_ = new QPushButton("✕", this);
    btnDelete_->setToolTip("Remove Widget");
    btnDelete_->setStyleSheet("QPushButton:hover { color: #ff5252; }");
    connect(btnDelete_, &QPushButton::clicked, this, [this]() {
        emit deleteRequested(index_);
    });
    layout->addWidget(btnDelete_);

    UpdateMetricOptions();
}

void LoadoutRowWidget::UpdateMetricOptions() {
    int curMetricId = comboMetric_->currentData().toInt();
    bool isNumeric = true;
    for (size_t i = 0; i < sizeof(kMetrics)/sizeof(kMetrics[0]); ++i) {
        if (kMetrics[i].id == curMetricId) {
            isNumeric = kMetrics[i].isNumeric;
            break;
        }
    }

    comboStat_->setEnabled(isNumeric);
    if (!isNumeric) {
        comboStat_->setCurrentIndex(0); // None
    }
}

void LoadoutRowWidget::OnMetricChanged(int) {
    widget_.metrics[0].metricId = comboMetric_->currentData().toInt();
    UpdateMetricOptions();
    emit changed();
}

void LoadoutRowWidget::OnStatChanged(int) {
    widget_.metrics[0].statId = comboStat_->currentData().toInt();
    emit changed();
}

void LoadoutRowWidget::OnTypeChanged(int) {
    widget_.widgetType = static_cast<WidgetType>(comboType_->currentData().toInt());
    bool isGraph = (widget_.widgetType == WidgetType::Graph);
    comboSubtype_->setEnabled(isGraph);
    btnColor_->setVisible(isGraph);
    emit changed();
}

void LoadoutRowWidget::OnSubtypeChanged(int) {
    widget_.graphType = static_cast<GraphType>(comboSubtype_->currentData().toInt());
    emit changed();
}

void LoadoutRowWidget::OnPickColor() {
    QColor chosen = QColorDialog::getColor(widget_.metrics[0].lineColor, this, "Select Graph Color", QColorDialog::ShowAlphaChannel);
    if (chosen.isValid()) {
        widget_.metrics[0].lineColor = chosen;
        btnColor_->setStyleSheet(QString("background-color: %1; border: 1px solid #4fc3f7; border-radius: 3px;").arg(chosen.name()));
        emit changed();
    }
}

LoadoutWidget LoadoutRowWidget::widget() const {
    return widget_;
}

} // namespace gnumon::gui
