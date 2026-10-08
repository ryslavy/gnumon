#pragma once

#include <QDialog>
#include <QCheckBox>
#include <QPushButton>

namespace gnumon::gui {

struct MetricsConfig {
    bool showGpuPower = true;
    bool showGpuTemp = true;
    bool showGpuFreq = true;
    bool showGpuUtil = true;
    bool showGpuVram = true;

    bool showCpuPower = true;
    bool showCpuTemp = true;
    bool showCpuFreq = true;
    bool showCpuUtil = true;

    bool showGraph = true;
};

class MetricsConfigDialog : public QDialog {
    Q_OBJECT

public:
    explicit MetricsConfigDialog(const MetricsConfig& currentConfig, QWidget *parent = nullptr);
    ~MetricsConfigDialog() override = default;

    MetricsConfig GetConfig() const;

private slots:
    void OnSelectAll();
    void OnResetDefaults();

private:
    QCheckBox *chkGpuPower_ = nullptr;
    QCheckBox *chkGpuTemp_ = nullptr;
    QCheckBox *chkGpuFreq_ = nullptr;
    QCheckBox *chkGpuUtil_ = nullptr;
    QCheckBox *chkGpuVram_ = nullptr;

    QCheckBox *chkCpuPower_ = nullptr;
    QCheckBox *chkCpuTemp_ = nullptr;
    QCheckBox *chkCpuFreq_ = nullptr;
    QCheckBox *chkCpuUtil_ = nullptr;

    QCheckBox *chkGraph_ = nullptr;
};

} // namespace gnumon::gui
