#pragma once

#include <QWidget>
#include <QComboBox>
#include <QPushButton>
#include <QLabel>
#include <QDoubleSpinBox>
#include <QCheckBox>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include "AppConfig.h"

namespace gnumon::gui {

class LoadoutRowWidget : public QWidget {
    Q_OBJECT

public:
    explicit LoadoutRowWidget(const LoadoutWidget& widget, int index, QWidget *parent = nullptr);

    LoadoutWidget widget() const;
    int index() const { return index_; }
    void setIndex(int idx) { index_ = idx; }

signals:
    void changed();
    void duplicateRequested(int index);
    void deleteRequested(int index);

private slots:
    void OnMetricChanged(int comboIdx);
    void OnTypeChanged(int comboIdx);
    void OnSubtypeChanged(int comboIdx);
    void OnToggleDetails();
    void OnAddLine();

private:
    void SetupUi();
    void RebuildLines();
    void UpdateMetricOptions();

    LoadoutWidget widget_;
    int index_ = 0;
    bool detailsExpanded_ = false;

    QVBoxLayout *mainLayout_ = nullptr;
    QHBoxLayout *topRowLayout_ = nullptr;
    QLabel *lblGrip_ = nullptr;
    QComboBox *comboMetric_ = nullptr;
    QComboBox *comboStatSingle_ = nullptr;
    QComboBox *comboType_ = nullptr;
    QComboBox *comboSubtype_ = nullptr;
    QPushButton *btnDetails_ = nullptr;
    QPushButton *btnAdd_ = nullptr;
    QPushButton *btnDelete_ = nullptr;

    QWidget *linesContainer_ = nullptr;
    QVBoxLayout *linesLayout_ = nullptr;

    QWidget *detailsPanel_ = nullptr;
    QCheckBox *chkAutoScale_ = nullptr;
    QDoubleSpinBox *spinRangeMin_ = nullptr;
    QDoubleSpinBox *spinRangeMax_ = nullptr;
    QPushButton *btnFillColor_ = nullptr;
};

} // namespace gnumon::gui
