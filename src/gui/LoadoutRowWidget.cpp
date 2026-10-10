#include "LoadoutRowWidget.h"
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QColorDialog>

namespace gnumon::gui {

struct MetricDef {
    int id;
    const char* name;
    bool isNumeric;
};

static const MetricDef kMetrics[] = {
    {138, "Between Display Change", true},
    {139, "Until Displayed", true},
    {144, "Dropped Frames", true},
    {12, "FPS-Presents", true},
    {11, "FPS-Displayed", true},
    {87, "FrameTime-Presents", true},
    {8, "FrameTime-App", true},
    {25, "Display Latency", true},
    {26, "Click to Photon Latency", true},
    {27, "Ms Animation Error", true},
    {33, "GPU Utilization", true},
    {28, "GPU Power", true},
    {31, "GPU Temperature", true},
    {30, "GPU Frequency", true},
    {29, "GPU Voltage", true},
    {47, "GPU VRAM", true},
    {3, "GPU Name", false},
    {34, "CPU Utilization", true},
    {89, "CPU Power", true},
    {90, "CPU Temperature", true},
    {91, "CPU Frequency", true},
    {9, "CPU Busy", true},
    {10, "CPU Wait", true},
    {5, "CPU Name", false}
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
    mainLayout_ = new QVBoxLayout(this);
    mainLayout_->setContentsMargins(8, 6, 8, 6);
    mainLayout_->setSpacing(6);

    setStyleSheet(
        "LoadoutRowWidget {"
        "  background-color: #161822;"
        "  border: 1px solid #282a3a;"
        "  border-radius: 4px;"
        "}"
        "QComboBox, QDoubleSpinBox {"
        "  background-color: #0f1017;"
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
        "  font-size: 13px;"
        "  padding: 4px;"
        "}"
        "QPushButton:hover {"
        "  color: #ffffff;"
        "}"
    );

    // --- TOP ROW ---
    auto *topRow = new QWidget(this);
    topRowLayout_ = new QHBoxLayout(topRow);
    topRowLayout_->setContentsMargins(0, 0, 0, 0);
    topRowLayout_->setSpacing(8);

    // Grip icon
    lblGrip_ = new QLabel("≡", topRow);
    lblGrip_->setStyleSheet("color: #70758a; font-size: 18px; font-weight: bold; padding: 0 4px;");
    lblGrip_->setFixedWidth(20);
    topRowLayout_->addWidget(lblGrip_);

    // Metric Combobox
    comboMetric_ = new QComboBox(topRow);
    int selectedMetricIdx = 0;
    for (size_t i = 0; i < sizeof(kMetrics)/sizeof(kMetrics[0]); ++i) {
        comboMetric_->addItem(kMetrics[i].name, kMetrics[i].id);
        if (kMetrics[i].id == widget_.metrics[0].metricId) {
            selectedMetricIdx = static_cast<int>(i);
        }
    }
    comboMetric_->setCurrentIndex(selectedMetricIdx);
    comboMetric_->setMinimumWidth(200);
    connect(comboMetric_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &LoadoutRowWidget::OnMetricChanged);
    topRowLayout_->addWidget(comboMetric_, 3);

    // Stat Combobox (only used when Readout)
    comboStatSingle_ = new QComboBox(topRow);
    comboStatSingle_->addItem("None", 0);
    comboStatSingle_->addItem("avg", 1);
    comboStatSingle_->addItem("1%", 5);
    comboStatSingle_->addItem("99%", 6);
    comboStatSingle_->addItem("raw", 4);
    comboStatSingle_->addItem("min", 2);
    comboStatSingle_->addItem("max", 3);
    int statIdx = comboStatSingle_->findData(widget_.metrics[0].statId);
    comboStatSingle_->setCurrentIndex(statIdx >= 0 ? statIdx : 1);
    comboStatSingle_->setMinimumWidth(80);
    connect(comboStatSingle_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) {
        if (!widget_.metrics.isEmpty()) {
            widget_.metrics[0].statId = comboStatSingle_->currentData().toInt();
            emit changed();
        }
    });
    topRowLayout_->addWidget(comboStatSingle_, 1);

    // Widget Type Combobox (Readout vs Graph)
    comboType_ = new QComboBox(topRow);
    comboType_->addItem("Readout", static_cast<int>(WidgetType::Readout));
    comboType_->addItem("Graph", static_cast<int>(WidgetType::Graph));
    comboType_->setCurrentIndex(widget_.widgetType == WidgetType::Graph ? 1 : 0);
    comboType_->setMinimumWidth(95);
    connect(comboType_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &LoadoutRowWidget::OnTypeChanged);
    topRowLayout_->addWidget(comboType_, 1);

    // Subtype Combobox (Line vs Histogram)
    comboSubtype_ = new QComboBox(topRow);
    comboSubtype_->addItem("Line", static_cast<int>(GraphType::Line));
    comboSubtype_->addItem("Histogram", static_cast<int>(GraphType::Histogram));
    comboSubtype_->setCurrentIndex(widget_.graphType == GraphType::Histogram ? 1 : 0);
    comboSubtype_->setMinimumWidth(95);
    connect(comboSubtype_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &LoadoutRowWidget::OnSubtypeChanged);
    topRowLayout_->addWidget(comboSubtype_, 1);

    // Details button
    btnDetails_ = new QPushButton("⚙ DETAILS", topRow);
    btnDetails_->setStyleSheet("background-color: #202432; color: #64b5f6; border: 1px solid #323a4e; border-radius: 3px; font-weight: bold; font-size: 11px; padding: 4px 10px;");
    connect(btnDetails_, &QPushButton::clicked, this, &LoadoutRowWidget::OnToggleDetails);
    topRowLayout_->addWidget(btnDetails_);

    // Add Line button (+)
    btnAdd_ = new QPushButton("+ LINE", topRow);
    btnAdd_->setStyleSheet("background-color: #1a2a3a; color: #00e5ff; border: 1px dashed #00b0ff; border-radius: 3px; font-weight: bold; font-size: 11px; padding: 4px 10px;");
    connect(btnAdd_, &QPushButton::clicked, this, &LoadoutRowWidget::OnAddLine);
    topRowLayout_->addWidget(btnAdd_);

    // Delete (✕)
    btnDelete_ = new QPushButton("✕", topRow);
    btnDelete_->setToolTip("Remove Widget");
    btnDelete_->setStyleSheet("color: #a0a4b8; font-size: 14px; font-weight: bold;");
    connect(btnDelete_, &QPushButton::clicked, this, [this]() {
        emit deleteRequested(index_);
    });
    topRowLayout_->addWidget(btnDelete_);

    mainLayout_->addWidget(topRow);

    // --- LINES CONTAINER (For Graph Widgets) ---
    linesContainer_ = new QWidget(this);
    linesLayout_ = new QVBoxLayout(linesContainer_);
    linesLayout_->setContentsMargins(28, 2, 8, 2);
    linesLayout_->setSpacing(4);
    mainLayout_->addWidget(linesContainer_);

    // --- DETAILS EXPANDER PANEL ---
    detailsPanel_ = new QWidget(this);
    detailsPanel_->setStyleSheet("background-color: #10121a; border: 1px solid #232736; border-radius: 4px; padding: 6px;");
    auto *detLayout = new QHBoxLayout(detailsPanel_);
    detLayout->setContentsMargins(12, 6, 12, 6);
    detLayout->setSpacing(14);

    chkAutoScale_ = new QCheckBox("Autoscale Left Axis", detailsPanel_);
    chkAutoScale_->setChecked(widget_.autoScale);
    chkAutoScale_->setStyleSheet("color: #e0e0e5; font-size: 12px;");
    connect(chkAutoScale_, &QCheckBox::toggled, this, [this](bool val) {
        widget_.autoScale = val;
        spinRangeMin_->setEnabled(!val);
        spinRangeMax_->setEnabled(!val);
        emit changed();
    });
    detLayout->addWidget(chkAutoScale_);

    auto *lblMin = new QLabel("Min:", detailsPanel_);
    lblMin->setStyleSheet("color: #8c90a4; font-size: 12px;");
    detLayout->addWidget(lblMin);
    spinRangeMin_ = new QDoubleSpinBox(detailsPanel_);
    spinRangeMin_->setRange(0.0, 5000.0);
    spinRangeMin_->setValue(widget_.rangeMin);
    spinRangeMin_->setEnabled(!widget_.autoScale);
    connect(spinRangeMin_, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this](double val) {
        widget_.rangeMin = static_cast<float>(val);
        emit changed();
    });
    detLayout->addWidget(spinRangeMin_);

    auto *lblMax = new QLabel("Max:", detailsPanel_);
    lblMax->setStyleSheet("color: #8c90a4; font-size: 12px;");
    detLayout->addWidget(lblMax);
    spinRangeMax_ = new QDoubleSpinBox(detailsPanel_);
    spinRangeMax_->setRange(0.1, 5000.0);
    spinRangeMax_->setValue(widget_.rangeMax);
    spinRangeMax_->setEnabled(!widget_.autoScale);
    connect(spinRangeMax_, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this](double val) {
        widget_.rangeMax = static_cast<float>(val);
        emit changed();
    });
    detLayout->addWidget(spinRangeMax_);

    btnFillColor_ = new QPushButton("Fill Color", detailsPanel_);
    QColor fc = widget_.metrics[0].fillColor;
    btnFillColor_->setStyleSheet(QString("background-color: %1; color: #fff; border: 1px solid #4a5568; border-radius: 3px; padding: 3px 10px; font-size: 11px;").arg(fc.name()));
    connect(btnFillColor_, &QPushButton::clicked, this, [this]() {
        QColor chosen = QColorDialog::getColor(widget_.metrics[0].fillColor, this, "Select Graph Area Fill Color", QColorDialog::ShowAlphaChannel);
        if (chosen.isValid()) {
            for (auto &m : widget_.metrics) {
                m.fillColor = chosen;
            }
            btnFillColor_->setStyleSheet(QString("background-color: %1; color: #fff; border: 1px solid #4a5568; border-radius: 3px; padding: 3px 10px; font-size: 11px;").arg(chosen.name()));
            emit changed();
        }
    });
    detLayout->addWidget(btnFillColor_);
    detLayout->addStretch();

    detailsPanel_->setVisible(false);
    mainLayout_->addWidget(detailsPanel_);

    UpdateMetricOptions();
    RebuildLines();
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

    bool isGraph = (widget_.widgetType == WidgetType::Graph);
    comboStatSingle_->setVisible(!isGraph);
    comboStatSingle_->setEnabled(isNumeric);
    comboSubtype_->setVisible(isGraph);
    btnDetails_->setVisible(isGraph);
    btnAdd_->setVisible(isGraph);
    linesContainer_->setVisible(isGraph);
    if (!isGraph) {
        detailsPanel_->setVisible(false);
        detailsExpanded_ = false;
    }
}

void LoadoutRowWidget::RebuildLines() {
    QLayoutItem *child;
    while ((child = linesLayout_->takeAt(0)) != nullptr) {
        if (child->widget()) child->widget()->deleteLater();
        delete child;
    }

    if (widget_.widgetType != WidgetType::Graph) {
        return;
    }

    for (int i = 0; i < widget_.metrics.size(); ++i) {
        auto *lineRow = new QWidget(linesContainer_);
        auto *lrLayout = new QHBoxLayout(lineRow);
        lrLayout->setContentsMargins(0, 2, 0, 2);
        lrLayout->setSpacing(8);

        // Color Swatch Button
        auto *btnColor = new QPushButton(lineRow);
        btnColor->setFixedSize(24, 20);
        QColor col = widget_.metrics[i].lineColor;
        btnColor->setStyleSheet(QString("background-color: %1; border: 1px solid #4fc3f7; border-radius: 3px;").arg(col.name()));
        connect(btnColor, &QPushButton::clicked, this, [this, i, btnColor]() {
            QColor chosen = QColorDialog::getColor(widget_.metrics[i].lineColor, this, "Select Series Color", QColorDialog::ShowAlphaChannel);
            if (chosen.isValid()) {
                widget_.metrics[i].lineColor = chosen;
                btnColor->setStyleSheet(QString("background-color: %1; border: 1px solid #4fc3f7; border-radius: 3px;").arg(chosen.name()));
                emit changed();
            }
        });
        lrLayout->addWidget(btnColor);

        // Label: Line 1, Line 2...
        auto *lblIdx = new QLabel(QString("Line %1:").arg(i + 1), lineRow);
        lblIdx->setStyleSheet("color: #8c90a4; font-size: 12px; font-weight: bold;");
        lblIdx->setFixedWidth(50);
        lrLayout->addWidget(lblIdx);

        // Stat selector for this series (avg, 99%, raw, etc.)
        auto *comboStat = new QComboBox(lineRow);
        comboStat->addItem("avg", 1);
        comboStat->addItem("99%", 6);
        comboStat->addItem("raw", 4);
        comboStat->addItem("1%", 5);
        comboStat->addItem("min", 2);
        comboStat->addItem("max", 3);
        int sIdx = comboStat->findData(widget_.metrics[i].statId);
        comboStat->setCurrentIndex(sIdx >= 0 ? sIdx : 0);
        comboStat->setFixedWidth(90);
        connect(comboStat, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this, i, comboStat](int) {
            widget_.metrics[i].statId = comboStat->currentData().toInt();
            emit changed();
        });
        lrLayout->addWidget(comboStat);

        lrLayout->addStretch();

        // Delete line button (only if more than 1 line)
        if (widget_.metrics.size() > 1) {
            auto *btnDelLine = new QPushButton("✕", lineRow);
            btnDelLine->setToolTip("Remove Series");
            btnDelLine->setStyleSheet("color: #70758a; font-size: 13px; font-weight: bold; padding: 2px 6px;");
            connect(btnDelLine, &QPushButton::clicked, this, [this, i]() {
                if (widget_.metrics.size() > 1 && i < widget_.metrics.size()) {
                    widget_.metrics.removeAt(i);
                    RebuildLines();
                    emit changed();
                }
            });
            lrLayout->addWidget(btnDelLine);
        }

        linesLayout_->addWidget(lineRow);
    }
}

void LoadoutRowWidget::OnMetricChanged(int) {
    int newMetricId = comboMetric_->currentData().toInt();
    for (auto &m : widget_.metrics) {
        m.metricId = newMetricId;
    }
    UpdateMetricOptions();
    emit changed();
}

void LoadoutRowWidget::OnTypeChanged(int) {
    widget_.widgetType = static_cast<WidgetType>(comboType_->currentData().toInt());
    UpdateMetricOptions();
    RebuildLines();
    emit changed();
}

void LoadoutRowWidget::OnSubtypeChanged(int) {
    widget_.graphType = static_cast<GraphType>(comboSubtype_->currentData().toInt());
    emit changed();
}

void LoadoutRowWidget::OnToggleDetails() {
    detailsExpanded_ = !detailsExpanded_;
    detailsPanel_->setVisible(detailsExpanded_ && widget_.widgetType == WidgetType::Graph);
    btnDetails_->setStyleSheet(detailsExpanded_ 
        ? "background-color: #1565c0; color: #ffffff; border: 1px solid #42a5f5; border-radius: 3px; font-weight: bold; font-size: 11px; padding: 4px 10px;"
        : "background-color: #202432; color: #64b5f6; border: 1px solid #323a4e; border-radius: 3px; font-weight: bold; font-size: 11px; padding: 4px 10px;");
}

void LoadoutRowWidget::OnAddLine() {
    if (widget_.widgetType == WidgetType::Graph) {
        LoadoutMetricItem item;
        item.metricId = comboMetric_->currentData().toInt();
        // Cycle default stat: 1 (avg) -> 6 (99%) -> 4 (raw)
        if (widget_.metrics.size() == 1) {
            item.statId = 6; // 99%
            item.lineColor = QColor(255, 82, 82); // Red
        } else if (widget_.metrics.size() == 2) {
            item.statId = 4; // raw
            item.lineColor = QColor(105, 240, 174); // Green
        } else {
            item.statId = 1;
            item.lineColor = QColor(255, 179, 0); // Amber
        }
        widget_.metrics.append(item);
        RebuildLines();
        emit changed();
    } else {
        emit duplicateRequested(index_);
    }
}

LoadoutWidget LoadoutRowWidget::widget() const {
    return widget_;
}

} // namespace gnumon::gui
