#include "MainViewWidget.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QScrollArea>
#include <QLineEdit>

namespace gnumon::gui {

static QWidget* CreateRowLabel(const QString& title, const QString& subtext) {
    auto *w = new QWidget();
    w->setStyleSheet("background: transparent; border: none;");
    auto *v = new QVBoxLayout(w);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(2);

    auto *lblTitle = new QLabel(title, w);
    lblTitle->setStyleSheet("color: #ffffff; font-size: 13px; font-weight: 500;");
    auto *lblSub = new QLabel(subtext, w);
    lblSub->setStyleSheet("color: #8c8f9e; font-size: 11px;");
    lblSub->setWordWrap(true);

    v->addWidget(lblTitle);
    v->addWidget(lblSub);
    return w;
}

MainViewWidget::MainViewWidget(AppConfig *config, QWidget *parent)
    : QWidget(parent), config_(config)
{
    SetupUi();
    ReloadFromConfig();
}

void MainViewWidget::SetupUi() {
    auto *rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(0, 0, 0, 0);

    auto *scrollArea = new QScrollArea(this);
    scrollArea->setWidgetResizable(true);
    scrollArea->setStyleSheet("border: none; background-color: #0b0c10;");

    auto *content = new QWidget(scrollArea);
    content->setStyleSheet("background-color: #0b0c10;");
    auto *layout = new QVBoxLayout(content);
    layout->setContentsMargins(40, 24, 40, 24);
    layout->setSpacing(20);

    auto cardStyle = 
        "background-color: #1a1b24;"
        "border: 1px solid #282937;"
        "border-radius: 6px;";

    // ==========================================
    // CARD 1: Process Tracking & Overlay Hotkey
    // ==========================================
    auto *card1 = new QWidget(content);
    card1->setStyleSheet(cardStyle);
    auto *grid1 = new QGridLayout(card1);
    grid1->setContentsMargins(24, 20, 24, 20);
    grid1->setVerticalSpacing(18);
    grid1->setHorizontalSpacing(24);
    grid1->setColumnStretch(0, 3);
    grid1->setColumnStretch(1, 7);

    // Row 1: Process
    grid1->addWidget(CreateRowLabel("Process", "Application process to track, overlay and capture"), 0, 0);
    comboProcess_ = new QComboBox(card1);
    comboProcess_->setEditable(true);
    comboProcess_->setInsertPolicy(QComboBox::NoInsert);
    comboProcess_->lineEdit()->setPlaceholderText("Select or type process...");
    comboProcess_->setStyleSheet(
        "QComboBox {"
        "  background-color: #12131a;"
        "  border: 1px solid #363948;"
        "  border-radius: 4px;"
        "  padding: 6px 12px;"
        "  color: #e0e0e5;"
        "  font-size: 13px;"
        "}"
    );
    connect(comboProcess_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &MainViewWidget::OnProcessComboChanged);
    grid1->addWidget(comboProcess_, 0, 1);

    // Row 2: Auto-target
    grid1->addWidget(CreateRowLabel("Auto-target", "Automatically target process with the highest GPU utilization"), 1, 0);
    swAutoTarget_ = new ToggleSwitch("Enable", card1);
    connect(swAutoTarget_, &ToggleSwitch::toggled, this, [this](bool val) {
        config_->autoTarget = val;
        comboProcess_->setEnabled(!val);
        config_->Save();
        emit configChanged();
    });
    grid1->addWidget(swAutoTarget_, 1, 1);

    // Row 3: Overlay Hotkey
    grid1->addWidget(CreateRowLabel("Overlay Hotkey", "Set hotkey to toggle overlay on/off"), 2, 0);
    auto *ovBox = new QHBoxLayout();
    ovBox->setSpacing(12);
    hpOverlay_ = new HotkeyPillWidget(config_->hotkeyOverlay, card1);
    connect(hpOverlay_, &HotkeyPillWidget::hotkeyChanged, this, [this](const QString& hk) {
        config_->hotkeyOverlay = hk;
        config_->Save();
        emit configChanged();
    });
    ovBox->addWidget(hpOverlay_);

    btnToggleOverlayAction_ = new QPushButton("TOGGLE OVERLAY", card1);
    btnToggleOverlayAction_->setStyleSheet(
        "QPushButton {"
        "  background-color: #1f2538;"
        "  color: #00e5ff;"
        "  border: 1px solid #364468;"
        "  border-radius: 4px;"
        "  padding: 6px 14px;"
        "  font-size: 11px;"
        "  font-weight: bold;"
        "}"
        "QPushButton:hover {"
        "  background-color: #2b3450;"
        "  border-color: #00e5ff;"
        "}"
    );
    btnToggleOverlayAction_->setCursor(Qt::PointingHandCursor);
    connect(btnToggleOverlayAction_, &QPushButton::clicked, this, &MainViewWidget::toggleOverlayRequested);
    ovBox->addWidget(btnToggleOverlayAction_);
    ovBox->addStretch();
    grid1->addLayout(ovBox, 2, 1);

    layout->addWidget(card1);

    // ==========================================
    // CARD 2: Preset Selection
    // ==========================================
    auto *card2 = new QWidget(content);
    card2->setStyleSheet(cardStyle);
    auto *grid2 = new QGridLayout(card2);
    grid2->setContentsMargins(24, 20, 24, 20);
    grid2->setVerticalSpacing(18);
    grid2->setHorizontalSpacing(24);
    grid2->setColumnStretch(0, 3);
    grid2->setColumnStretch(1, 7);

    // Row 1: Preset
    grid2->addWidget(CreateRowLabel("Preset", "Select a preset configuration for overlay widget loadout etc."), 0, 0);
    auto *presetBox = new QHBoxLayout();
    presetBox->setSpacing(2);

    const QStringList presetNames = {"BASIC", "GAME EXPERIENCE", "GPU FOCUS", "POWER/TEMP", "CUSTOM"};
    for (int i = 0; i < presetNames.size(); ++i) {
        auto *btn = new QPushButton(presetNames[i], card2);
        btn->setStyleSheet(
            "QPushButton {"
            "  background-color: #161821;"
            "  color: #a0a4b8;"
            "  border: 1px solid #323545;"
            "  padding: 8px 14px;"
            "  font-size: 11px;"
            "  font-weight: bold;"
            "  border-radius: 0px;"
            "}"
            "QPushButton:hover {"
            "  background-color: #202330;"
            "  color: #ffffff;"
            "}"
        );
        btn->setCursor(Qt::PointingHandCursor);
        connect(btn, &QPushButton::clicked, this, [this, i]() {
            OnPresetButtonClicked(i);
        });
        presetButtons_.append(btn);
        presetBox->addWidget(btn);
    }

    btnEditPreset_ = new QPushButton("EDIT", card2);
    btnEditPreset_->setStyleSheet(
        "QPushButton {"
        "  background-color: #1976d2;"
        "  color: #ffffff;"
        "  border: none;"
        "  border-radius: 4px;"
        "  padding: 8px 18px;"
        "  font-size: 11px;"
        "  font-weight: bold;"
        "  margin-left: 12px;"
        "}"
        "QPushButton:hover {"
        "  background-color: #2196f3;"
        "}"
    );
    btnEditPreset_->setCursor(Qt::PointingHandCursor);
    connect(btnEditPreset_, &QPushButton::clicked, this, &MainViewWidget::editLoadoutRequested);
    presetBox->addWidget(btnEditPreset_);
    presetBox->addStretch();
    grid2->addLayout(presetBox, 0, 1);

    // Row 2: Preset Cycle Hotkey
    grid2->addWidget(CreateRowLabel("Preset Cycle Hotkey", "Set hotkey for cycling through presets"), 1, 0);
    hpPresetCycle_ = new HotkeyPillWidget(config_->hotkeyPresetCycle, card2);
    connect(hpPresetCycle_, &HotkeyPillWidget::hotkeyChanged, this, [this](const QString& hk) {
        config_->hotkeyPresetCycle = hk;
        config_->Save();
        emit configChanged();
    });
    grid2->addWidget(hpPresetCycle_, 1, 1, Qt::AlignLeft);

    layout->addWidget(card2);

    // ==========================================
    // CARD 3: Capture Configuration
    // ==========================================
    auto *card3 = new QWidget(content);
    card3->setStyleSheet(cardStyle);
    auto *grid3 = new QGridLayout(card3);
    grid3->setContentsMargins(24, 20, 24, 20);
    grid3->setVerticalSpacing(18);
    grid3->setHorizontalSpacing(24);
    grid3->setColumnStretch(0, 3);
    grid3->setColumnStretch(1, 7);

    // Row 1: Capture Duration
    grid3->addWidget(CreateRowLabel("Capture Duration", "Automatically stop capture after N seconds"), 0, 0);
    auto *durBox = new QHBoxLayout();
    swDuration_ = new ToggleSwitch("Enable", card3);
    spinDuration_ = new QSpinBox(card3);
    spinDuration_->setRange(1, 3600);
    spinDuration_->setValue(config_->captureDurationSeconds);
    spinDuration_->setSuffix(" s");
    spinDuration_->setEnabled(config_->enableCaptureDuration);
    spinDuration_->setStyleSheet(
        "QSpinBox {"
        "  background-color: #12131a;"
        "  border: 1px solid #363948;"
        "  border-radius: 4px;"
        "  padding: 4px 10px;"
        "  color: #e0e0e5;"
        "  font-weight: bold;"
        "}"
    );

    connect(swDuration_, &ToggleSwitch::toggled, this, [this](bool val) {
        config_->enableCaptureDuration = val;
        spinDuration_->setEnabled(val);
        config_->Save();
        emit configChanged();
    });
    connect(spinDuration_, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int val) {
        config_->captureDurationSeconds = val;
        config_->Save();
        emit configChanged();
    });

    durBox->addWidget(swDuration_);
    durBox->addSpacing(16);
    durBox->addWidget(new QLabel("Seconds:", card3));
    durBox->addWidget(spinDuration_);
    durBox->addStretch();
    grid3->addLayout(durBox, 0, 1);

    // Row 2: Capture Hotkey
    grid3->addWidget(CreateRowLabel("Capture Hotkey", "Set hotkey for capture of per-frame performance data as CSV"), 1, 0);
    auto *capBox = new QHBoxLayout();
    capBox->setSpacing(12);
    hpCapture_ = new HotkeyPillWidget(config_->hotkeyCapture, card3);
    connect(hpCapture_, &HotkeyPillWidget::hotkeyChanged, this, [this](const QString& hk) {
        config_->hotkeyCapture = hk;
        config_->Save();
        emit configChanged();
    });
    capBox->addWidget(hpCapture_);

    btnToggleCaptureAction_ = new QPushButton("START CAPTURE", card3);
    btnToggleCaptureAction_->setStyleSheet(
        "QPushButton {"
        "  background-color: #2b1f24;"
        "  color: #ff5252;"
        "  border: 1px solid #5a323a;"
        "  border-radius: 4px;"
        "  padding: 6px 14px;"
        "  font-size: 11px;"
        "  font-weight: bold;"
        "}"
        "QPushButton:hover {"
        "  background-color: #3d2830;"
        "  border-color: #ff5252;"
        "}"
    );
    btnToggleCaptureAction_->setCursor(Qt::PointingHandCursor);
    connect(btnToggleCaptureAction_, &QPushButton::clicked, this, &MainViewWidget::toggleCaptureRequested);
    capBox->addWidget(btnToggleCaptureAction_);
    capBox->addStretch();
    grid3->addLayout(capBox, 1, 1);

    layout->addWidget(card3);

    // ==========================================
    // CARD 4: Capture Storage
    // ==========================================
    auto *card4 = new QWidget(content);
    card4->setStyleSheet(cardStyle);
    auto *grid4 = new QGridLayout(card4);
    grid4->setContentsMargins(24, 20, 24, 20);
    grid4->setVerticalSpacing(18);
    grid4->setHorizontalSpacing(24);
    grid4->setColumnStretch(0, 3);
    grid4->setColumnStretch(1, 7);

    grid4->addWidget(CreateRowLabel("Capture Storage", "Open the folder containing all frame traces and stats summaries"), 0, 0);
    btnOpenExplorer_ = new QPushButton("OPEN IN EXPLORER", card4);
    btnOpenExplorer_->setStyleSheet(
        "QPushButton {"
        "  background-color: #2b2e3c;"
        "  color: #e0e0e5;"
        "  border: 1px solid #3d4154;"
        "  border-radius: 4px;"
        "  padding: 8px 24px;"
        "  font-size: 11px;"
        "  font-weight: bold;"
        "}"
        "QPushButton:hover {"
        "  background-color: #383c4e;"
        "  border-color: #555b73;"
        "}"
    );
    btnOpenExplorer_->setCursor(Qt::PointingHandCursor);
    connect(btnOpenExplorer_, &QPushButton::clicked, this, &MainViewWidget::openCapturesRequested);
    grid4->addWidget(btnOpenExplorer_, 0, 1, Qt::AlignLeft);

    layout->addWidget(card4);

    // Settings link row at bottom right
    auto *settingsLinkRow = new QHBoxLayout();
    settingsLinkRow->addStretch();
    btnSettingsLink_ = new QPushButton("Settings ⚙", content);
    btnSettingsLink_->setStyleSheet(
        "QPushButton {"
        "  background: transparent;"
        "  color: #a0a4b8;"
        "  border: none;"
        "  font-size: 20px;"
        "  font-weight: 500;"
        "  padding: 6px 12px;"
        "}"
        "QPushButton:hover {"
        "  color: #ffffff;"
        "}"
    );
    btnSettingsLink_->setCursor(Qt::PointingHandCursor);
    connect(btnSettingsLink_, &QPushButton::clicked, this, &MainViewWidget::settingsRequested);
    settingsLinkRow->addWidget(btnSettingsLink_);
    layout->addLayout(settingsLinkRow);

    layout->addStretch();

    scrollArea->setWidget(content);
    rootLayout->addWidget(scrollArea);
}

void MainViewWidget::ReloadFromConfig() {
    if (!config_) return;
    {
        QSignalBlocker b1(swAutoTarget_);
        swAutoTarget_->setChecked(config_->autoTarget);
    }
    comboProcess_->setEnabled(!config_->autoTarget);
    hpOverlay_->setHotkey(config_->hotkeyOverlay);
    hpPresetCycle_->setHotkey(config_->hotkeyPresetCycle);
    hpCapture_->setHotkey(config_->hotkeyCapture);
    {
        QSignalBlocker b2(swDuration_);
        swDuration_->setChecked(config_->enableCaptureDuration);
    }
    {
        QSignalBlocker b3(spinDuration_);
        spinDuration_->setValue(config_->captureDurationSeconds);
    }
    spinDuration_->setEnabled(config_->enableCaptureDuration);

    int pIdx = config_->selectedPreset;
    if (pIdx == 1000) pIdx = 4; // Custom
    UpdatePresetButtonStyles(pIdx);
}

void MainViewWidget::UpdatePresetButtonStyles(int idx) {
    for (int i = 0; i < presetButtons_.size(); ++i) {
        if (i == idx) {
            presetButtons_[i]->setStyleSheet(
                "QPushButton {"
                "  background-color: #242938;"
                "  color: #ffffff;"
                "  border: 1px solid #1976d2;"
                "  padding: 8px 14px;"
                "  font-size: 11px;"
                "  font-weight: bold;"
                "  border-radius: 0px;"
                "}"
            );
        } else {
            presetButtons_[i]->setStyleSheet(
                "QPushButton {"
                "  background-color: #161821;"
                "  color: #a0a4b8;"
                "  border: 1px solid #323545;"
                "  padding: 8px 14px;"
                "  font-size: 11px;"
                "  font-weight: bold;"
                "  border-radius: 0px;"
                "}"
                "QPushButton:hover {"
                "  background-color: #202330;"
                "  color: #ffffff;"
                "}"
            );
        }
    }
}

void MainViewWidget::OnPresetButtonClicked(int idx) {
    int configPreset = (idx == 4) ? 1000 : idx;
    config_->ApplyPreset(configPreset);
    config_->Save();
    UpdatePresetButtonStyles(idx);
    emit configChanged();
}

void MainViewWidget::SetProcessList(const QStringList& processes, const QVector<uint32_t>& pids) {
    pids_ = pids;
    comboProcess_->blockSignals(true);
    comboProcess_->clear();
    comboProcess_->addItem("None (System-wide / No target)", 0);

    int targetIdx = 0;
    for (int i = 0; i < processes.size(); ++i) {
        comboProcess_->addItem(processes[i], pids[i]);
        if (pids[i] == selectedPid_) {
            targetIdx = i + 1;
        }
    }
    comboProcess_->setCurrentIndex(targetIdx);
    comboProcess_->blockSignals(false);
}

void MainViewWidget::setSelectedPid(uint32_t pid) {
    selectedPid_ = pid;
    for (int i = 0; i < comboProcess_->count(); ++i) {
        if (comboProcess_->itemData(i).toUInt() == pid) {
            comboProcess_->blockSignals(true);
            comboProcess_->setCurrentIndex(i);
            comboProcess_->blockSignals(false);
            break;
        }
    }
}

void MainViewWidget::OnProcessComboChanged(int index) {
    if (index >= 0) {
        selectedPid_ = comboProcess_->itemData(index).toUInt();
        QString name = comboProcess_->currentText();
        emit processChanged(selectedPid_, name);
    }
}

void MainViewWidget::SetRecordingActive(bool active) {
    if (!btnToggleCaptureAction_) return;
    if (active) {
        btnToggleCaptureAction_->setText("● STOP CAPTURE");
        btnToggleCaptureAction_->setStyleSheet(
            "QPushButton {"
            "  background-color: #5c1818;"
            "  color: #ffffff;"
            "  border: 1px solid #ff5252;"
            "  border-radius: 4px;"
            "  padding: 6px 14px;"
            "  font-size: 11px;"
            "  font-weight: bold;"
            "}"
            "QPushButton:hover {"
            "  background-color: #7a2020;"
            "}"
        );
    } else {
        btnToggleCaptureAction_->setText("START CAPTURE");
        btnToggleCaptureAction_->setStyleSheet(
            "QPushButton {"
            "  background-color: #2b1f24;"
            "  color: #ff5252;"
            "  border: 1px solid #5a323a;"
            "  border-radius: 4px;"
            "  padding: 6px 14px;"
            "  font-size: 11px;"
            "  font-weight: bold;"
            "}"
            "QPushButton:hover {"
            "  background-color: #3d2830;"
            "  border-color: #ff5252;"
            "}"
        );
    }
}

void MainViewWidget::SetOverlayActive(bool active) {
    if (!btnToggleOverlayAction_) return;
    btnToggleOverlayAction_->setText(active ? "HIDE OVERLAY" : "SHOW OVERLAY");
}

} // namespace gnumon::gui
