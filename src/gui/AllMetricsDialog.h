#pragma once

#include <QDialog>
#include <QLabel>
#include <QPushButton>
#include <QTabWidget>
#include <QTableWidget>
#include <QProgressBar>
#include <QLineEdit>
#include <QComboBox>
#include <QCheckBox>
#include <QTimer>
#include <vector>
#include "MetricGraphWidget.h"
#include "../../include/gnumon/PresentMonAPI.h"

namespace gnumon::gui {

class AllMetricsDialog : public QDialog {
    Q_OBJECT

public:
    explicit AllMetricsDialog(PM_SESSION_HANDLE session, uint32_t processId, QWidget *parent = nullptr);
    ~AllMetricsDialog() override = default;

    void SetTargetProcess(uint32_t pid);
    void SelectMetricForChart(int metricId);

private slots:
    void OnRefreshTimer();
    void OnTogglePause();
    void OnCopyJson();
    void OnExportCsv();
    void OnSearchFilterChanged(const QString& text);
    void OnPrimaryMetricChanged(int index);
    void OnSecondaryMetricChanged(int index);
    void OnSecondaryToggle(bool checked);
    void OnTimeWindowChanged(int index);
    void OnTableMetricDoubleClicked(int row, int col);

private:
    void SetupUi();
    void SetupOverviewTab(QTabWidget *tabs);
    void SetupLiveChartTab(QTabWidget *tabs);
    void SetupPacingTab(QTabWidget *tabs);
    void SetupLatencyTab(QTabWidget *tabs);
    void SetupGpuTab(QTabWidget *tabs);
    void SetupVramTab(QTabWidget *tabs);
    void SetupCpuTab(QTabWidget *tabs);
    void SetupPsoTab(QTabWidget *tabs);
    void SetupDictionaryTab(QTabWidget *tabs);

    void UpdateDashboard(const PM_FULL_TELEMETRY_SNAPSHOT& s);
    void UpdateLiveChart(const PM_FULL_TELEMETRY_SNAPSHOT& s);
    void UpdatePacing(const PM_FULL_TELEMETRY_SNAPSHOT& s);
    void UpdateLatency(const PM_FULL_TELEMETRY_SNAPSHOT& s);
    void UpdateGpu(const PM_FULL_TELEMETRY_SNAPSHOT& s);
    void UpdateVram(const PM_FULL_TELEMETRY_SNAPSHOT& s);
    void UpdateCpu(const PM_FULL_TELEMETRY_SNAPSHOT& s);
    void UpdatePso(const PM_FULL_TELEMETRY_SNAPSHOT& s);
    void UpdateDictionary(const PM_FULL_TELEMETRY_SNAPSHOT& s);

    PM_SESSION_HANDLE session_ = nullptr;
    uint32_t processId_ = 0;
    QTimer *refreshTimer_ = nullptr;
    bool isPaused_ = false;

    // Header elements
    QLabel *lblProcessTitle_ = nullptr;
    QLabel *lblRuntimeBadge_ = nullptr;
    QLabel *lblPresentModeBadge_ = nullptr;
    QLabel *lblSwapchain_ = nullptr;
    QLabel *lblStatusBadge_ = nullptr;
    QPushButton *btnPauseResume_ = nullptr;

    // Overview Tab
    QLabel *lblFpsCurrent_ = nullptr;
    QLabel *lblFpsSub_ = nullptr;
    QLabel *lblFtCurrent_ = nullptr;
    QLabel *lblFtSub_ = nullptr;
    QLabel *lblLatencyCurrent_ = nullptr;
    QLabel *lblLatencySub_ = nullptr;
    QLabel *lblAnimErrCurrent_ = nullptr;
    QLabel *lblAnimErrSub_ = nullptr;
    QLabel *lblGpuOverview_ = nullptr;
    QLabel *lblGpuOverviewSub_ = nullptr;
    QProgressBar *barGpuOverview_ = nullptr;
    QLabel *lblCpuOverview_ = nullptr;
    QLabel *lblCpuOverviewSub_ = nullptr;
    QProgressBar *barCpuOverview_ = nullptr;
    QLabel *lblVramOverview_ = nullptr;
    QLabel *lblVramOverviewSub_ = nullptr;
    QProgressBar *barVramOverview_ = nullptr;
    QLabel *lblSyncOverview_ = nullptr;
    QLabel *lblSyncOverviewSub_ = nullptr;

    // Pacing Tab
    QLabel *lblPacingAppFt_ = nullptr;
    QLabel *lblPacingDispFt_ = nullptr;
    QLabel *lblPacingPresFt_ = nullptr;
    QLabel *lblPacingAvgFt_ = nullptr;
    QLabel *lblPacing99pFt_ = nullptr;
    QLabel *lblPacingInApi_ = nullptr;
    QLabel *lblPacingUntilDisp_ = nullptr;
    QLabel *lblPacingBetweenPres_ = nullptr;
    QLabel *lblPacingFlipDelay_ = nullptr;
    QLabel *lblPacingDropped_ = nullptr;
    QLabel *lblPacingSyncInt_ = nullptr;
    QLabel *lblPacingTearing_ = nullptr;

    // Latency Tab
    QLabel *lblLatPc_ = nullptr;
    QLabel *lblLatDisp_ = nullptr;
    QLabel *lblLatClick_ = nullptr;
    QLabel *lblLatInput_ = nullptr;
    QLabel *lblLatRenderPres_ = nullptr;
    QLabel *lblLatAnimErr_ = nullptr;
    QLabel *lblLatAnimTime_ = nullptr;
    QLabel *lblLatRatingBadge_ = nullptr;
    QLabel *lblStutterRatingBadge_ = nullptr;

    // GPU Tab
    QLabel *lblGpuName_ = nullptr;
    QLabel *lblGpuClock_ = nullptr;
    QLabel *lblGpuTempEdge_ = nullptr;
    QLabel *lblGpuTempHotspot_ = nullptr;
    QLabel *lblGpuPower_ = nullptr;
    QLabel *lblGpuVoltage_ = nullptr;
    QLabel *lblGpuFan_ = nullptr;
    QLabel *lblGpuUtil_ = nullptr;
    QProgressBar *barGpuUtil_ = nullptr;
    QLabel *lblGpuTime_ = nullptr;
    QLabel *lblGpuBusy_ = nullptr;
    QLabel *lblGpuWait_ = nullptr;
    QLabel *lblLimitPower_ = nullptr;
    QLabel *lblLimitTemp_ = nullptr;
    QLabel *lblLimitVoltage_ = nullptr;
    QLabel *lblLimitCurrent_ = nullptr;
    QLabel *lblLimitUtil_ = nullptr;

    // VRAM Tab
    QLabel *lblVramCapacity_ = nullptr;
    QLabel *lblVramUtil_ = nullptr;
    QProgressBar *barVramUtil_ = nullptr;
    QLabel *lblVramClock_ = nullptr;
    QLabel *lblVramBandwidth_ = nullptr;
    QLabel *lblVramTemp_ = nullptr;
    QLabel *lblVramLimitPower_ = nullptr;
    QLabel *lblVramLimitTemp_ = nullptr;

    // CPU Tab
    QLabel *lblCpuName_ = nullptr;
    QLabel *lblCpuCores_ = nullptr;
    QLabel *lblCpuUtil_ = nullptr;
    QProgressBar *barCpuUtil_ = nullptr;
    QLabel *lblCpuPower_ = nullptr;
    QLabel *lblCpuTemp_ = nullptr;
    QLabel *lblCpuClock_ = nullptr;
    QLabel *lblCpuBusy_ = nullptr;
    QLabel *lblCpuWait_ = nullptr;
    QWidget *coreContainer_ = nullptr;
    std::vector<QProgressBar*> coreProgressBars_;
    std::vector<QLabel*> coreLabels_;

    // PSO Tab
    QLabel *lblPsoCount_ = nullptr;
    QLabel *lblPsoTime_ = nullptr;
    QLabel *lblPsoBusy_ = nullptr;

    // Table Tab
    QLineEdit *searchFilter_ = nullptr;
    QTableWidget *metricsTable_ = nullptr;
    struct MetricRowDef {
        int metricId;
        QString name;
        QString category;
        QString unit;
        QString description;
    };
    std::vector<MetricRowDef> metricDefs_;

    // Live Chart Tab
    QTabWidget *tabWidget_ = nullptr;
    MetricGraphWidget *metricGraph_ = nullptr;
    QComboBox *comboPrimary_ = nullptr;
    QComboBox *comboSecondary_ = nullptr;
    QCheckBox *checkSecondary_ = nullptr;
    QComboBox *comboWindow_ = nullptr;
    QLabel *lblChartCur_ = nullptr;
    QLabel *lblChartMin_ = nullptr;
    QLabel *lblChartMax_ = nullptr;
    QLabel *lblChartAvg_ = nullptr;
    QLabel *lblChartP99_ = nullptr;

    PM_FULL_TELEMETRY_SNAPSHOT lastSnapshot_{};
};

} // namespace gnumon::gui
