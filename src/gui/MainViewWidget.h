#pragma once

#include <QWidget>
#include <QComboBox>
#include <QPushButton>
#include <QSpinBox>
#include <QLabel>
#include "AppConfig.h"
#include "ToggleSwitch.h"
#include "HotkeyPillWidget.h"

namespace gnumon::gui {

class MainViewWidget : public QWidget {
    Q_OBJECT

public:
    explicit MainViewWidget(AppConfig *config, QWidget *parent = nullptr);

    void ReloadFromConfig();
    void SetProcessList(const QStringList& processes, const QVector<uint32_t>& pids);
    uint32_t selectedPid() const { return selectedPid_; }
    void setSelectedPid(uint32_t pid);

signals:
    void processChanged(uint32_t pid, const QString& name);
    void editLoadoutRequested();
    void settingsRequested();
    void openCapturesRequested();
    void configChanged();

private slots:
    void OnProcessComboChanged(int index);
    void OnPresetButtonClicked(int presetIdx);

private:
    void SetupUi();
    QWidget* CreateCard();

    AppConfig *config_ = nullptr;
    uint32_t selectedPid_ = 0;
    QVector<uint32_t> pids_;

    QComboBox *comboProcess_ = nullptr;
    ToggleSwitch *swAutoTarget_ = nullptr;
    HotkeyPillWidget *hpOverlay_ = nullptr;

    QVector<QPushButton*> presetButtons_;
    QPushButton *btnEditPreset_ = nullptr;
    HotkeyPillWidget *hpPresetCycle_ = nullptr;

    ToggleSwitch *swDuration_ = nullptr;
    QSpinBox *spinDuration_ = nullptr;
    HotkeyPillWidget *hpCapture_ = nullptr;

    QPushButton *btnOpenExplorer_ = nullptr;
    QPushButton *btnSettingsLink_ = nullptr;
};

} // namespace gnumon::gui
