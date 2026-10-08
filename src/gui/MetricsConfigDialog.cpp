#include "MetricsConfigDialog.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QDialogButtonBox>

namespace gnumon::gui {

MetricsConfigDialog::MetricsConfigDialog(const MetricsConfig& currentConfig, QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle("Configure Tracked Metrics — gnumon");
    setModal(true);
    resize(420, 360);

    auto *mainLayout = new QVBoxLayout(this);

    // --- GPU Group ---
    auto *gpuGroup = new QGroupBox("GPU Metrics", this);
    auto *gpuLayout = new QGridLayout(gpuGroup);

    chkGpuPower_ = new QCheckBox("GPU Power (W)", this);
    chkGpuPower_->setChecked(currentConfig.showGpuPower);
    chkGpuTemp_ = new QCheckBox("GPU Temperature (°C)", this);
    chkGpuTemp_->setChecked(currentConfig.showGpuTemp);
    chkGpuFreq_ = new QCheckBox("GPU Clock (MHz)", this);
    chkGpuFreq_->setChecked(currentConfig.showGpuFreq);
    chkGpuUtil_ = new QCheckBox("GPU Utilization (%)", this);
    chkGpuUtil_->setChecked(currentConfig.showGpuUtil);
    chkGpuVram_ = new QCheckBox("GPU VRAM Allocation (MB)", this);
    chkGpuVram_->setChecked(currentConfig.showGpuVram);

    gpuLayout->addWidget(chkGpuPower_, 0, 0);
    gpuLayout->addWidget(chkGpuTemp_, 0, 1);
    gpuLayout->addWidget(chkGpuFreq_, 1, 0);
    gpuLayout->addWidget(chkGpuUtil_, 1, 1);
    gpuLayout->addWidget(chkGpuVram_, 2, 0, 1, 2);

    mainLayout->addWidget(gpuGroup);

    // --- CPU Group ---
    auto *cpuGroup = new QGroupBox("CPU Metrics", this);
    auto *cpuLayout = new QGridLayout(cpuGroup);

    chkCpuPower_ = new QCheckBox("Package Power (W)", this);
    chkCpuPower_->setChecked(currentConfig.showCpuPower);
    chkCpuTemp_ = new QCheckBox("CPU Temperature (°C)", this);
    chkCpuTemp_->setChecked(currentConfig.showCpuTemp);
    chkCpuFreq_ = new QCheckBox("CPU Clock (MHz)", this);
    chkCpuFreq_->setChecked(currentConfig.showCpuFreq);
    chkCpuUtil_ = new QCheckBox("CPU Utilization (%)", this);
    chkCpuUtil_->setChecked(currentConfig.showCpuUtil);

    cpuLayout->addWidget(chkCpuPower_, 0, 0);
    cpuLayout->addWidget(chkCpuTemp_, 0, 1);
    cpuLayout->addWidget(chkCpuFreq_, 1, 0);
    cpuLayout->addWidget(chkCpuUtil_, 1, 1);

    mainLayout->addWidget(cpuGroup);

    // --- General Group ---
    auto *genGroup = new QGroupBox("Display & Visualization", this);
    auto *genLayout = new QVBoxLayout(genGroup);
    chkGraph_ = new QCheckBox("Show Real-Time Frametime History Graph", this);
    chkGraph_->setChecked(currentConfig.showGraph);
    genLayout->addWidget(chkGraph_);
    mainLayout->addWidget(genGroup);

    // --- Action Buttons ---
    auto *btnLayout = new QHBoxLayout();
    auto *btnSelectAll = new QPushButton("Select All", this);
    connect(btnSelectAll, &QPushButton::clicked, this, &MetricsConfigDialog::OnSelectAll);

    auto *btnReset = new QPushButton("Defaults", this);
    connect(btnReset, &QPushButton::clicked, this, &MetricsConfigDialog::OnResetDefaults);

    btnLayout->addWidget(btnSelectAll);
    btnLayout->addWidget(btnReset);
    btnLayout->addStretch();

    mainLayout->addLayout(btnLayout);

    // Dialog buttons (OK / Cancel)
    auto *buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttonBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    mainLayout->addWidget(buttonBox);
}

void MetricsConfigDialog::OnSelectAll() {
    chkGpuPower_->setChecked(true);
    chkGpuTemp_->setChecked(true);
    chkGpuFreq_->setChecked(true);
    chkGpuUtil_->setChecked(true);
    chkGpuVram_->setChecked(true);

    chkCpuPower_->setChecked(true);
    chkCpuTemp_->setChecked(true);
    chkCpuFreq_->setChecked(true);
    chkCpuUtil_->setChecked(true);

    chkGraph_->setChecked(true);
}

void MetricsConfigDialog::OnResetDefaults() {
    OnSelectAll();
}

MetricsConfig MetricsConfigDialog::GetConfig() const {
    MetricsConfig cfg;
    cfg.showGpuPower = chkGpuPower_->isChecked();
    cfg.showGpuTemp = chkGpuTemp_->isChecked();
    cfg.showGpuFreq = chkGpuFreq_->isChecked();
    cfg.showGpuUtil = chkGpuUtil_->isChecked();
    cfg.showGpuVram = chkGpuVram_->isChecked();

    cfg.showCpuPower = chkCpuPower_->isChecked();
    cfg.showCpuTemp = chkCpuTemp_->isChecked();
    cfg.showCpuFreq = chkCpuFreq_->isChecked();
    cfg.showCpuUtil = chkCpuUtil_->isChecked();

    cfg.showGraph = chkGraph_->isChecked();
    return cfg;
}

} // namespace gnumon::gui
