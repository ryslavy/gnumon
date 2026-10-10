#pragma once

#include <QWidget>
#include <QComboBox>
#include <QPushButton>
#include <QLabel>
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
    void OnStatChanged(int comboIdx);
    void OnTypeChanged(int comboIdx);
    void OnSubtypeChanged(int comboIdx);
    void OnPickColor();

private:
    void SetupUi();
    void UpdateMetricOptions();

    LoadoutWidget widget_;
    int index_ = 0;

    QLabel *lblGrip_ = nullptr;
    QComboBox *comboMetric_ = nullptr;
    QComboBox *comboStat_ = nullptr;
    QComboBox *comboType_ = nullptr;
    QComboBox *comboSubtype_ = nullptr;
    QPushButton *btnColor_ = nullptr;
    QPushButton *btnDetails_ = nullptr;
    QPushButton *btnAdd_ = nullptr;
    QPushButton *btnDelete_ = nullptr;
};

} // namespace gnumon::gui
