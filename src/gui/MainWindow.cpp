#include "MainWindow.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QDateTime>
#include <QDir>
#include <QStandardPaths>
#include <QDesktopServices>
#include <QUrl>
#include <QProcess>
#include "GuiUtils.h"
#include <QIcon>
#include <QFileInfo>
#include <QKeySequence>
#include <QMessageBox>
#include <filesystem>
#include <iostream>
#include <iomanip>
#include <QRegularExpression>
#include "../common/ProcUtils.h"

namespace gnumon::gui {

struct FramePayload {
    uint32_t processId = 0;
    uint64_t swapChain = 0;
    int32_t runtime = 3;
    int32_t syncInterval = 1;
    uint32_t presentFlags = 0;
    bool allowsTearing = false;
    int32_t presentMode = 0;
    int32_t frameType = 2;
    uint64_t cpuStartQpc = 0;
    double frameTimeMs = 0.0;
    double cpuBusyMs = 0.0;
    double cpuWaitMs = 0.0;
    double gpuLatencyMs = 0.0;
    double gpuTimeMs = 0.0;
    double gpuBusyMs = 0.0;
    double gpuWaitMs = 0.0;
    double videoBusyMs = 0.0;
    double displayLatencyMs = 0.0;
    double displayedTimeMs = 0.0;
    double animationError = 0.0;
    double animationTime = 0.0;
    double msFlipDelay = 0.0;
    double allInputToPhotonLatency = 0.0;
    double clickToPhotonLatencyMs = 0.0;
    double instrumentedLatencyMs = 0.0;
    double gpuPower = 0.0;
    double gpuTemp = 0.0;
    double gpuUtil = 0.0;
    double gpuFreq = 0.0;
    double cpuPower = 0.0;
    double cpuTemp = 0.0;
    double cpuUtil = 0.0;
};

static const char* FormatRuntime(int32_t runtime) {
    switch (runtime) {
    case 1: return "DXGI";
    case 2: return "D3D9";
    case 3: return "Vulkan";
    case 4: return "OpenGL";
    default: return "Vulkan";
    }
}

static const char* FormatPresentMode(int32_t mode) {
    switch (mode) {
    case 0: return "Hardware: Legacy Flip";
    case 1: return "Hardware: Legacy Copy to front buffer";
    case 2: return "Hardware: Independent Flip";
    case 3: return "Composed: Flip";
    case 4: return "Hardware Composed: Independent Flip";
    case 5: return "Composed: Copy with GPU GDI";
    case 6: return "Composed: Copy with CPU GDI";
    default: return "Hardware: Legacy Flip";
    }
}

static const char* FormatFrameType(int32_t type) {
    switch (type) {
    case 100: return "AMD_AFMF";
    case 50: return "Intel XeSS-FG";
    default: return "Application";
    }
}

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    config_.Load();

    setWindowTitle("Intel PresentMon");
    setMinimumSize(960, 620);
    resize(1060, 720);

    // Set Window Icon if exists
    QString iconPath = ":/packaging/gnumon.svg";
    if (!QFileInfo::exists(iconPath)) {
        iconPath = "/home/vaclav/Dokumenty/Vyvoj/gnumon/gnumon-linux/packaging/gnumon.svg";
    }
    if (QFileInfo::exists(iconPath)) {
        setWindowIcon(QIcon(iconPath));
    }

    SetupUi();

    // Initialize PresentMon API Session
    if (pmOpenSession(&session_) != PM_STATUS_SUCCESS) {
        std::cerr << "[gnumon-gui] Note: pmOpenSession will retry on next poll." << std::endl;
    }

    pollTimer_ = new QTimer(this);
    connect(pollTimer_, &QTimer::timeout, this, &MainWindow::OnPollTimer);
    int interval = (config_.dataPollingRate > 0) ? (1000 / config_.dataPollingRate) : 25;
    pollTimer_->start(interval);

    procRefreshTimer_ = new QTimer(this);
    connect(procRefreshTimer_, &QTimer::timeout, this, &MainWindow::RefreshProcesses);
    procRefreshTimer_->start(2000);
    RefreshProcesses();

    UpdateStatusBar();

    windowedOverlay_ = new WindowedOverlayWidget(&config_, nullptr);
    connect(windowedOverlay_, &WindowedOverlayWidget::windowedClosed, this, &MainWindow::OnConfigChanged);
    if (config_.overlayWindowedMode) {
        windowedOverlay_->show();
    }
}

MainWindow::~MainWindow() {
    if (windowedOverlay_) {
        delete windowedOverlay_;
        windowedOverlay_ = nullptr;
    }
    if (isRecording_) {
        OnToggleRecording();
    }
    if (frameQuery_ && session_) {
        pmFreeFrameQuery(frameQuery_);
        frameQuery_ = nullptr;
    }
    if (session_) {
        pmCloseSession(session_);
        session_ = nullptr;
    }
}

void MainWindow::SetupUi() {
    auto *central = new QWidget(this);
    setCentralWidget(central);
    auto *mainLayout = new QVBoxLayout(central);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    rootStack_ = new QStackedWidget(central);
    mainView_ = new MainViewWidget(&config_, rootStack_);
    settingsPage_ = new SettingsPage(&config_, rootStack_);
    loadoutPage_ = new LoadoutConfigPage(&config_, rootStack_);

    rootStack_->addWidget(mainView_);      // Index 0
    rootStack_->addWidget(settingsPage_);  // Index 1
    rootStack_->addWidget(loadoutPage_);   // Index 2

    // Connect View Navigation (No Dialogs!)
    connect(mainView_, &MainViewWidget::settingsRequested, this, &MainWindow::OnOpenSettings);
    connect(mainView_, &MainViewWidget::editLoadoutRequested, this, &MainWindow::OnEditLoadout);
    connect(mainView_, &MainViewWidget::openCapturesRequested, this, &MainWindow::OnOpenCapturesFolder);
    connect(mainView_, &MainViewWidget::processChanged, this, &MainWindow::OnProcessChanged);
    connect(mainView_, &MainViewWidget::toggleOverlayRequested, this, &MainWindow::OnToggleOverlay);
    connect(mainView_, &MainViewWidget::toggleCaptureRequested, this, &MainWindow::OnToggleRecording);
    connect(mainView_, &MainViewWidget::configChanged, this, &MainWindow::OnConfigChanged);

    connect(settingsPage_, &SettingsPage::topRequested, this, &MainWindow::OnBackToMain);
    connect(settingsPage_, &SettingsPage::configChanged, this, &MainWindow::OnConfigChanged);

    connect(loadoutPage_, &LoadoutConfigPage::backRequested, this, &MainWindow::OnBackToMain);
    connect(loadoutPage_, &LoadoutConfigPage::loadoutChanged, this, &MainWindow::OnConfigChanged);

    mainLayout->addWidget(rootStack_, 1);

    // ==========================================
    // Bottom Status Bar (Blue #1565C0, 24px)
    // ==========================================
    statusBarWidget_ = new QWidget(central);
    statusBarWidget_->setFixedHeight(24);
    statusBarWidget_->setStyleSheet("background-color: #1565c0; color: #ffffff;");

    auto *statusLayout = new QHBoxLayout(statusBarWidget_);
    statusLayout->setContentsMargins(12, 0, 12, 0);
    statusLayout->setSpacing(12);

    lblStatusProcess_ = new QLabel("", statusBarWidget_);
    lblStatusProcess_->setStyleSheet("font-size: 12px; font-weight: 500; color: #ffffff;");

    lblStatusRec_ = new QLabel("● REC", statusBarWidget_);
    lblStatusRec_->setStyleSheet("color: #ff5252; font-weight: bold; font-size: 11px; background-color: #2b0b0b; padding: 1px 6px; border-radius: 2px;");
    lblStatusRec_->setVisible(false);

    statusLayout->addWidget(lblStatusProcess_);
    statusLayout->addWidget(lblStatusRec_);
    statusLayout->addStretch();

    lblStatusHide_ = new QLabel("Autohide", statusBarWidget_);
    lblStatusHide_->setStyleSheet("font-size: 12px; font-weight: 300; color: #e3f2fd;");

    lblStatusPoll_ = new QLabel(QString("%1Hz").arg(config_.dataPollingRate), statusBarWidget_);
    lblStatusPoll_->setStyleSheet("font-size: 12px; font-weight: 300; color: #e3f2fd;");

    lblStatusFps_ = new QLabel(QString("%1fps").arg(config_.overlayDrawRate), statusBarWidget_);
    lblStatusFps_->setStyleSheet("font-size: 12px; font-weight: 300; color: #e3f2fd;");

    statusLayout->addWidget(lblStatusHide_);
    statusLayout->addWidget(lblStatusPoll_);
    statusLayout->addWidget(lblStatusFps_);

    mainLayout->addWidget(statusBarWidget_);
}

void MainWindow::OnOpenSettings() {
    settingsPage_->ReloadFromConfig();
    rootStack_->setCurrentWidget(settingsPage_);
}

void MainWindow::OnEditLoadout() {
    loadoutPage_->ReloadFromConfig();
    rootStack_->setCurrentWidget(loadoutPage_);
}

void MainWindow::OnOpenFullMetrics() {
    OnEditLoadout();
}

void MainWindow::OnBackToMain() {
    mainView_->ReloadFromConfig();
    UpdateStatusBar();
    rootStack_->setCurrentWidget(mainView_);
}

void MainWindow::OnConfigChanged() {
    UpdateStatusBar();
    if (windowedOverlay_) {
        if (config_.overlayWindowedMode) {
            windowedOverlay_->ReloadLayout();
            windowedOverlay_->show();
            windowedOverlay_->raise();
            windowedOverlay_->activateWindow();
        } else {
            windowedOverlay_->hide();
        }
    }
    if (pollTimer_ && config_.dataPollingRate > 0) {
        int interval = 1000 / config_.dataPollingRate;
        if (pollTimer_->interval() != interval) {
            pollTimer_->setInterval(interval);
        }
    }
}

void MainWindow::UpdateStatusBar() {
    if (trackedPid_ > 0) {
        lblStatusProcess_->setText(QString("%1 [%2]").arg(trackedProcessName_).arg(trackedPid_));
    } else {
        lblStatusProcess_->setText("No target process");
    }

    lblStatusRec_->setVisible(isRecording_);

    if (config_.overlayHideDuringCapture && isRecording_) {
        lblStatusHide_->setText("(Auto)Hidden");
    } else if (config_.overlayHideDuringCapture) {
        lblStatusHide_->setText("Autohide");
    } else {
        lblStatusHide_->setText(inGameOverlayActive_ ? "Visible" : "Hidden");
    }

    lblStatusPoll_->setText(QString("%1Hz").arg(config_.dataPollingRate));
    lblStatusFps_->setText(QString("%1fps").arg(config_.overlayDrawRate));
}

void MainWindow::RefreshProcesses() {
    auto running = common::ProcUtils::GetRunningProcesses();
    QStringList names;
    QVector<uint32_t> pids;

    // Filter non-gaming applications if Target Block List is enabled
    static const QStringList blockList = {
        "systemd", "kwin", "gnome-shell", "Xwayland", "pipewire", "pulseaudio",
        "bash", "zsh", "sh", "ninja", "cmake", "code", "cursor", "antigravity",
        "sshd", "dbus", "udevd", "sed", "grep", "cat", "find", "python3"
    };

    for (const auto& proc : running) {
        QString qName = QString::fromStdString(proc.name);
        if (config_.captureTargetBlockList) {
            bool blocked = false;
            for (const auto& b : blockList) {
                if (qName.contains(b, Qt::CaseInsensitive)) {
                    blocked = true;
                    break;
                }
            }
            if (blocked) continue;
        }

        QString itemStr;
        if (!proc.windowTitle.empty()) {
            itemStr = QString("%1 — %2 [%3]")
                          .arg(QString::fromStdString(proc.windowTitle))
                          .arg(qName)
                          .arg(proc.pid);
        } else {
            itemStr = QString("%1 [%2]").arg(qName).arg(proc.pid);
        }

        names.append(itemStr);
        pids.append(proc.pid);
    }

    mainView_->SetProcessList(names, pids);
    if (config_.autoTarget) {
        AutoTargetProcess();
    }
}

void MainWindow::AutoTargetProcess() {
    auto activeRings = common::GetActiveRingPids();
    if (!activeRings.empty()) {
        uint32_t topPid = activeRings.front();
        if (topPid != trackedPid_) {
            trackedPid_ = topPid;
            trackedProcessName_ = QString::fromStdString(common::ProcUtils::GetProcessName(trackedPid_));
            mainView_->setSelectedPid(trackedPid_);
            if (session_) {
                pmStartTrackingProcess(session_, trackedPid_);
            }
            UpdateStatusBar();
        }
    } else if (trackedPid_ > 0 && !common::IsRingAlive(trackedPid_)) {
        trackedPid_ = 0;
        trackedProcessName_ = "";
        UpdateStatusBar();
    }
}

void MainWindow::OnProcessChanged(uint32_t pid, const QString& name) {
    trackedPid_ = pid;
    trackedProcessName_ = name.section(" [", 0, 0).section(" — ", -1);
    if (session_ && trackedPid_ > 0) {
        pmStartTrackingProcess(session_, trackedPid_);
    }
    UpdateStatusBar();
}

void MainWindow::OnToggleOverlay() {
    inGameOverlayActive_ = !inGameOverlayActive_;
    if (session_) {
        pmSetInGameOverlayState(session_, inGameOverlayActive_);
    }
    UpdateStatusBar();
}

void MainWindow::OnCyclePreset() {
    int nextPreset = 0;
    if (config_.selectedPreset == 0) nextPreset = 1;
    else if (config_.selectedPreset == 1) nextPreset = 2;
    else if (config_.selectedPreset == 2) nextPreset = 3;
    else if (config_.selectedPreset == 3) nextPreset = 0;
    else nextPreset = 0;

    config_.ApplyPreset(nextPreset);
    config_.Save();
    mainView_->ReloadFromConfig();
    UpdateStatusBar();
}

void MainWindow::OnToggleRecording() {
    SetRecording(!isRecording_);
}

void MainWindow::SetRecording(bool active) {
    if (isRecording_ == active) return;
    isRecording_ = active;

    if (isRecording_) {
        // Auto-acquire active game PID if trackedPid_ is 0 or dead
        bool trackedHasRing = (trackedPid_ > 0 && common::IsRingAlive(trackedPid_));
        if (!trackedHasRing) {
            auto activePids = common::GetActiveRingPids();
            if (!activePids.empty()) {
                trackedPid_ = activePids.front();
                trackedProcessName_ = QString::fromStdString(common::ProcUtils::GetProcessName(trackedPid_));
            }
        }
        if (session_) {
            pmStartTrackingProcess(session_, trackedPid_);
            pmSetRecordingState(session_, true);
        }

        QString appName = trackedProcessName_.isEmpty() ? "Application" : trackedProcessName_;
        appName.remove(QRegularExpression("[^a-zA-Z0-9_.-]"));
        QString timestamp = QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss");
        QString filename = QString("PresentMon-%1_%2.csv").arg(appName, timestamp);
        currentCapturePath_ = GetCapturesDirectory() + "/" + filename;
        csvFile_.open(currentCapturePath_.toStdString());
        if (!csvFile_.is_open()) {
            QMessageBox::critical(this, "Capture Error", QString("Could not create CSV file at:\n%1").arg(currentCapturePath_));
            isRecording_ = false;
            if (session_) {
                pmSetRecordingState(session_, false);
            }
            return;
        }

        // Write official Intel PresentMon CSV Header
        csvFile_ << "Application,ProcessID,SwapChainAddress,PresentRuntime,SyncInterval,PresentFlags,AllowsTearing,PresentMode,"
                 << "FrameType,CPUStartQPC,FrameTime,FPS,CPUBusy,CPUWait,GPULatency,GPUTime,GPUBusy,GPUWait,VideoBusy,"
                 << "DisplayLatency,DisplayedTime,AnimationError,AnimationTime,MsFlipDelay,AllInputToPhotonLatency,"
                 << "ClickToPhotonLatency,InstrumentedLatency,GPUPowerW,GPUTemperatureC,GPUUtilizationPercent,GPUFrequencyMHz,"
                 << "CPUPowerW,CPUTemperatureC,CPUUtilizationPercent\n";
        csvFile_.flush();
        captureStats_.Reset();

        std::vector<PM_QUERY_ELEMENT> frameElements = {
            { PM_METRIC_PROCESS_ID, PM_STAT_NONE, 0, 0, offsetof(FramePayload, processId), sizeof(uint32_t) },
            { PM_METRIC_SWAP_CHAIN_ADDRESS, PM_STAT_NONE, 0, 0, offsetof(FramePayload, swapChain), sizeof(uint64_t) },
            { PM_METRIC_PRESENT_RUNTIME, PM_STAT_NONE, 0, 0, offsetof(FramePayload, runtime), sizeof(int32_t) },
            { PM_METRIC_SYNC_INTERVAL, PM_STAT_NONE, 0, 0, offsetof(FramePayload, syncInterval), sizeof(int32_t) },
            { PM_METRIC_PRESENT_FLAGS, PM_STAT_NONE, 0, 0, offsetof(FramePayload, presentFlags), sizeof(uint32_t) },
            { PM_METRIC_ALLOWS_TEARING, PM_STAT_NONE, 0, 0, offsetof(FramePayload, allowsTearing), sizeof(bool) },
            { PM_METRIC_PRESENT_MODE, PM_STAT_NONE, 0, 0, offsetof(FramePayload, presentMode), sizeof(int32_t) },
            { PM_METRIC_FRAME_TYPE, PM_STAT_NONE, 0, 0, offsetof(FramePayload, frameType), sizeof(int32_t) },
            { PM_METRIC_CPU_START_QPC, PM_STAT_NONE, 0, 0, offsetof(FramePayload, cpuStartQpc), sizeof(uint64_t) },
            { PM_METRIC_CPU_FRAME_TIME, PM_STAT_NONE, 0, 0, offsetof(FramePayload, frameTimeMs), sizeof(double) },
            { PM_METRIC_CPU_BUSY, PM_STAT_NONE, 0, 0, offsetof(FramePayload, cpuBusyMs), sizeof(double) },
            { PM_METRIC_CPU_WAIT, PM_STAT_NONE, 0, 0, offsetof(FramePayload, cpuWaitMs), sizeof(double) },
            { PM_METRIC_GPU_LATENCY, PM_STAT_NONE, 0, 0, offsetof(FramePayload, gpuLatencyMs), sizeof(double) },
            { PM_METRIC_GPU_TIME, PM_STAT_NONE, 0, 0, offsetof(FramePayload, gpuTimeMs), sizeof(double) },
            { PM_METRIC_GPU_BUSY, PM_STAT_NONE, 0, 0, offsetof(FramePayload, gpuBusyMs), sizeof(double) },
            { PM_METRIC_GPU_WAIT, PM_STAT_NONE, 0, 0, offsetof(FramePayload, gpuWaitMs), sizeof(double) },
            { PM_METRIC_DISPLAY_LATENCY, PM_STAT_NONE, 0, 0, offsetof(FramePayload, displayLatencyMs), sizeof(double) },
            { PM_METRIC_UNTIL_DISPLAYED, PM_STAT_NONE, 0, 0, offsetof(FramePayload, displayedTimeMs), sizeof(double) },
            { PM_METRIC_ANIMATION_ERROR, PM_STAT_NONE, 0, 0, offsetof(FramePayload, animationError), sizeof(double) },
            { PM_METRIC_ANIMATION_TIME, PM_STAT_NONE, 0, 0, offsetof(FramePayload, animationTime), sizeof(double) },
            { PM_METRIC_ALL_INPUT_TO_PHOTON_LATENCY, PM_STAT_NONE, 0, 0, offsetof(FramePayload, allInputToPhotonLatency), sizeof(double) },
            { PM_METRIC_CLICK_TO_PHOTON_LATENCY, PM_STAT_NONE, 0, 0, offsetof(FramePayload, clickToPhotonLatencyMs), sizeof(double) },
            { PM_METRIC_INSTRUMENTED_LATENCY, PM_STAT_NONE, 0, 0, offsetof(FramePayload, instrumentedLatencyMs), sizeof(double) },
            { PM_METRIC_GPU_POWER, PM_STAT_NONE, 0, 0, offsetof(FramePayload, gpuPower), sizeof(double) },
            { PM_METRIC_GPU_TEMPERATURE, PM_STAT_NONE, 0, 0, offsetof(FramePayload, gpuTemp), sizeof(double) },
            { PM_METRIC_GPU_UTILIZATION, PM_STAT_NONE, 0, 0, offsetof(FramePayload, gpuUtil), sizeof(double) },
            { PM_METRIC_GPU_FREQUENCY, PM_STAT_NONE, 0, 0, offsetof(FramePayload, gpuFreq), sizeof(double) },
            { PM_METRIC_CPU_POWER, PM_STAT_NONE, 0, 0, offsetof(FramePayload, cpuPower), sizeof(double) },
            { PM_METRIC_CPU_TEMPERATURE, PM_STAT_NONE, 0, 0, offsetof(FramePayload, cpuTemp), sizeof(double) },
            { PM_METRIC_CPU_UTILIZATION, PM_STAT_NONE, 0, 0, offsetof(FramePayload, cpuUtil), sizeof(double) },
        };

        if (frameQuery_) {
            pmFreeFrameQuery(frameQuery_);
            frameQuery_ = nullptr;
        }
        pmRegisterFrameQuery(session_, &frameQuery_, frameElements.data(), frameElements.size(), &frameBlobSize_);

        recordedFramesCount_ = 0;
        recordingStartMs_ = QDateTime::currentMSecsSinceEpoch();
    } else {
        if (session_) {
            pmSetRecordingState(session_, false);
        }
        if (frameQuery_ && session_ && csvFile_.is_open() && trackedPid_ > 0) {
            // Drain remaining frames before closing
            constexpr uint32_t BATCH_SIZE = 128;
            std::vector<uint8_t> buffer(BATCH_SIZE * frameBlobSize_);
            uint32_t numFrames = BATCH_SIZE;
            while (pmConsumeFrames(frameQuery_, trackedPid_, buffer.data(), &numFrames) == PM_STATUS_SUCCESS && numFrames > 0) {
                for (uint32_t i = 0; i < numFrames; ++i) {
                    auto* frame = reinterpret_cast<FramePayload*>(buffer.data() + (i * frameBlobSize_));
                    captureStats_.AddFrame(frame->frameTimeMs, frame->gpuUtil, frame->gpuTemp, frame->gpuPower,
                                           frame->gpuFreq, frame->cpuUtil, frame->cpuTemp, frame->cpuPower);
                    std::string appName = trackedProcessName_.isEmpty() ? "Unknown" : trackedProcessName_.toStdString();
                    double fpsVal = (frame->frameTimeMs > 0.0001) ? (1000.0 / frame->frameTimeMs) : 0.0;
                    csvFile_ << appName << ","
                             << frame->processId << ","
                             << "0x" << std::hex << frame->swapChain << std::dec << ","
                             << FormatRuntime(frame->runtime) << ","
                             << frame->syncInterval << ","
                             << frame->presentFlags << ","
                             << (frame->allowsTearing ? 1 : 0) << ","
                             << "\"" << FormatPresentMode(frame->presentMode) << "\","
                             << FormatFrameType(frame->frameType) << ","
                             << frame->cpuStartQpc << ","
                             << std::fixed << std::setprecision(4)
                             << frame->frameTimeMs << ","
                             << std::setprecision(2)
                             << fpsVal << ","
                             << frame->cpuBusyMs << ","
                             << frame->cpuWaitMs << ","
                             << frame->gpuLatencyMs << ","
                             << frame->gpuTimeMs << ","
                             << frame->gpuBusyMs << ","
                             << frame->gpuWaitMs << ","
                             << frame->videoBusyMs << ","
                             << frame->displayLatencyMs << ","
                             << frame->displayedTimeMs << ","
                             << frame->animationError << ","
                             << frame->animationTime << ","
                             << frame->msFlipDelay << ","
                             << frame->allInputToPhotonLatency << ","
                             << frame->clickToPhotonLatencyMs << ","
                             << frame->instrumentedLatencyMs << ","
                             << std::setprecision(2)
                             << frame->gpuPower << ","
                             << frame->gpuTemp << ","
                             << frame->gpuUtil << ","
                             << frame->gpuFreq << ","
                             << frame->cpuPower << ","
                             << frame->cpuTemp << ","
                             << frame->cpuUtil << "\n";
                    recordedFramesCount_++;
                }
                if (numFrames < BATCH_SIZE) break;
                numFrames = BATCH_SIZE;
            }
        }
        if (csvFile_.is_open()) {
            WriteCaptureSummary();
            csvFile_.flush();
            csvFile_.close();
        }
        if (frameQuery_) {
            pmFreeFrameQuery(frameQuery_);
            frameQuery_ = nullptr;
        }
    }

    UpdateStatusBar();
}

void MainWindow::OnPollTimer() {
    // If autoTarget is enabled, or if current tracked PID is dead/invalid, dynamically auto-acquire active ring
    if (config_.autoTarget || trackedPid_ == 0 || !common::IsRingAlive(trackedPid_)) {
        AutoTargetProcess();
    }

    // 1. Sync In-Game and Engine recording/overlay state
    if (session_) {
        bool ringRec = false;
        bool hasRing = (pmGetRecordingState(session_, &ringRec) == PM_STATUS_SUCCESS &&
                        trackedPid_ > 0 && common::IsRingAlive(trackedPid_));

        if (hasRing) {
            // When an active game ring exists, the game layer is the primary authority for in-game hotkeys.
            // Adopt ring recording state idempotently.
            if (ringRec != isRecording_) {
                SetRecording(ringRec);
            }
            // Adopt ring overlay state idempotently.
            bool ringOverlay = inGameOverlayActive_;
            if (pmGetInGameOverlayState(session_, &ringOverlay) == PM_STATUS_SUCCESS) {
                inGameOverlayActive_ = ringOverlay;
            }
            // Drain/discard any coordinator hotkey events to avoid ghost triggers while game is active
            bool dummy = false;
            pmCheckRecordHotkeyTriggered(session_, &dummy);
            pmCheckInGameOverlayHotkeyTriggered(session_, &dummy);
        } else {
            // No active game ring: use global hotkey events from coordinator (desktop/windowed mode)
            bool recTriggered = false;
            if (pmCheckRecordHotkeyTriggered(session_, &recTriggered) == PM_STATUS_SUCCESS && recTriggered) {
                SetRecording(!isRecording_);
            }
            bool hudTriggered = false;
            if (pmCheckInGameOverlayHotkeyTriggered(session_, &hudTriggered) == PM_STATUS_SUCCESS && hudTriggered) {
                inGameOverlayActive_ = !inGameOverlayActive_;
            }
        }
    }
    if (mainView_) {
        mainView_->SetRecordingActive(isRecording_);
        mainView_->SetOverlayActive(inGameOverlayActive_);
    }

    // 2. Process frame recording if active
    if (isRecording_ && csvFile_.is_open()) {
        // Check capture duration auto-stop
        if (config_.enableCaptureDuration && config_.captureDurationSeconds > 0) {
            uint64_t elapsedSec = (QDateTime::currentMSecsSinceEpoch() - recordingStartMs_) / 1000;
            if (elapsedSec >= static_cast<uint64_t>(config_.captureDurationSeconds)) {
                SetRecording(false);
                return;
            }
        }

        if (frameQuery_) {
            constexpr uint32_t BATCH_SIZE = 128;
            std::vector<uint8_t> buffer(BATCH_SIZE * frameBlobSize_);
            uint32_t numFrames = BATCH_SIZE;

            while (pmConsumeFrames(frameQuery_, trackedPid_, buffer.data(), &numFrames) == PM_STATUS_SUCCESS && numFrames > 0) {
                for (uint32_t i = 0; i < numFrames; ++i) {
                    auto* frame = reinterpret_cast<FramePayload*>(buffer.data() + (i * frameBlobSize_));
                    captureStats_.AddFrame(frame->frameTimeMs, frame->gpuUtil, frame->gpuTemp, frame->gpuPower,
                                           frame->gpuFreq, frame->cpuUtil, frame->cpuTemp, frame->cpuPower);
                    std::string appName = trackedProcessName_.isEmpty() ? "Unknown" : trackedProcessName_.toStdString();
                    double fpsVal = (frame->frameTimeMs > 0.0001) ? (1000.0 / frame->frameTimeMs) : 0.0;
                    csvFile_ << appName << ","
                             << frame->processId << ","
                             << "0x" << std::hex << frame->swapChain << std::dec << ","
                             << FormatRuntime(frame->runtime) << ","
                             << frame->syncInterval << ","
                             << frame->presentFlags << ","
                             << (frame->allowsTearing ? 1 : 0) << ","
                             << "\"" << FormatPresentMode(frame->presentMode) << "\","
                             << FormatFrameType(frame->frameType) << ","
                             << frame->cpuStartQpc << ","
                             << std::fixed << std::setprecision(4)
                             << frame->frameTimeMs << ","
                             << std::setprecision(2)
                             << fpsVal << ","
                             << frame->cpuBusyMs << ","
                             << frame->cpuWaitMs << ","
                             << frame->gpuLatencyMs << ","
                             << frame->gpuTimeMs << ","
                             << frame->gpuBusyMs << ","
                             << frame->gpuWaitMs << ","
                             << frame->videoBusyMs << ","
                             << frame->displayLatencyMs << ","
                             << frame->displayedTimeMs << ","
                             << frame->animationError << ","
                             << frame->animationTime << ","
                             << frame->msFlipDelay << ","
                             << frame->allInputToPhotonLatency << ","
                             << frame->clickToPhotonLatencyMs << ","
                             << frame->instrumentedLatencyMs << ","
                             << std::setprecision(2)
                             << frame->gpuPower << ","
                             << frame->gpuTemp << ","
                             << frame->gpuUtil << ","
                             << frame->gpuFreq << ","
                             << frame->cpuPower << ","
                             << frame->cpuTemp << ","
                             << frame->cpuUtil << "\n";
                    recordedFramesCount_++;
                }
                if (numFrames < BATCH_SIZE) break;
                numFrames = BATCH_SIZE;
            }
            csvFile_.flush();
        }
    }

    // 3. Update Windowed Overlay if active
    if (windowedOverlay_ && windowedOverlay_->isVisible() && session_) {
        PM_FULL_TELEMETRY_SNAPSHOT snap{};
        if (pmGetFullTelemetrySnapshot(session_, trackedPid_, &snap) == PM_STATUS_SUCCESS) {
            windowedOverlay_->UpdateFromSnapshot(snap);
        }
    }

    UpdateStatusBar();
}

void MainWindow::WriteCaptureSummary() {
    if (!csvFile_.is_open()) return;

    double durationSec = (recordingStartMs_ > 0)
        ? (static_cast<double>(QDateTime::currentMSecsSinceEpoch() - recordingStartMs_) / 1000.0)
        : 0.0;
    size_t totalFrames = captureStats_.frameTimesMs.size();

    double avgFps = (durationSec > 0.0) ? (static_cast<double>(totalFrames) / durationSec) : 0.0;
    double onePercentLowFps = 0.0;
    double pointOnePercentLowFps = 0.0;
    double minFps = 0.0;
    double maxFps = 0.0;

    double avgFt = 0.0;
    double p99Ft = 0.0;
    double p999Ft = 0.0;
    double minFt = 0.0;
    double maxFt = 0.0;

    if (!captureStats_.fpsList.empty()) {
        std::vector<double> sortedFps = captureStats_.fpsList;
        std::sort(sortedFps.begin(), sortedFps.end());
        minFps = sortedFps.front();
        maxFps = sortedFps.back();
        size_t idx01 = static_cast<size_t>(std::floor(sortedFps.size() * 0.01));
        onePercentLowFps = sortedFps[std::min(idx01, sortedFps.size() - 1)];
        size_t idx001 = static_cast<size_t>(std::floor(sortedFps.size() * 0.001));
        pointOnePercentLowFps = sortedFps[std::min(idx001, sortedFps.size() - 1)];
    }

    if (!captureStats_.frameTimesMs.empty()) {
        std::vector<double> sortedFt = captureStats_.frameTimesMs;
        std::sort(sortedFt.begin(), sortedFt.end());
        minFt = sortedFt.front();
        maxFt = sortedFt.back();
        double sumFt = 0.0;
        for (double ft : sortedFt) sumFt += ft;
        avgFt = sumFt / static_cast<double>(sortedFt.size());
        size_t idx99 = static_cast<size_t>(std::floor(sortedFt.size() * 0.99));
        p99Ft = sortedFt[std::min(idx99, sortedFt.size() - 1)];
        size_t idx999 = static_cast<size_t>(std::floor(sortedFt.size() * 0.999));
        p999Ft = sortedFt[std::min(idx999, sortedFt.size() - 1)];
    }

    double avgGpuUtil = captureStats_.sampleCount ? (captureStats_.sumGpuUtil / captureStats_.sampleCount) : 0.0;
    double avgGpuTemp = captureStats_.sampleCount ? (captureStats_.sumGpuTemp / captureStats_.sampleCount) : 0.0;
    double avgGpuPower = captureStats_.sampleCount ? (captureStats_.sumGpuPower / captureStats_.sampleCount) : 0.0;
    double avgGpuFreq = captureStats_.sampleCount ? (captureStats_.sumGpuFreq / captureStats_.sampleCount) : 0.0;
    double avgCpuUtil = captureStats_.sampleCount ? (captureStats_.sumCpuUtil / captureStats_.sampleCount) : 0.0;
    double avgCpuTemp = captureStats_.sampleCount ? (captureStats_.sumCpuTemp / captureStats_.sampleCount) : 0.0;
    double avgCpuPower = captureStats_.sampleCount ? (captureStats_.sumCpuPower / captureStats_.sampleCount) : 0.0;

    std::string appName = trackedProcessName_.isEmpty() ? "Unknown" : trackedProcessName_.toStdString();
    std::string dateTimeStr = QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss").toStdString();

    // 1. Append Benchmark Summary block to CSV
    csvFile_ << "\n# ========================================================\n"
             << "# GNUMON BENCHMARK SUMMARY\n"
             << "# ========================================================\n"
             << "# Application:           " << appName << "\n"
             << "# Process ID:            " << trackedPid_ << "\n"
             << "# Date / Time:           " << dateTimeStr << "\n"
             << "# Benchmark Duration:    " << std::fixed << std::setprecision(2) << durationSec << " s\n"
             << "# Total Recorded Frames: " << totalFrames << "\n"
             << "#\n"
             << "# --- Frame Rate (FPS) ---\n"
             << "# Average FPS:           " << avgFps << "\n"
             << "# 1% Low FPS:            " << onePercentLowFps << "\n"
             << "# 0.1% Low FPS:          " << pointOnePercentLowFps << "\n"
             << "# Minimum FPS:           " << minFps << "\n"
             << "# Maximum FPS:           " << maxFps << "\n"
             << "#\n"
             << "# --- Frame Time (ms) ---\n"
             << "# Average Frame Time:    " << avgFt << " ms\n"
             << "# 99th Percentile:       " << p99Ft << " ms\n"
             << "# 99.9th Percentile:     " << p999Ft << " ms\n"
             << "# Minimum Frame Time:    " << minFt << " ms\n"
             << "# Maximum Frame Time:    " << maxFt << " ms\n"
             << "#\n"
             << "# --- Hardware Telemetry (Averages) ---\n"
             << "# GPU Utilization:       " << avgGpuUtil << " %\n"
             << "# GPU Temperature:       " << avgGpuTemp << " C\n"
             << "# GPU Power:             " << avgGpuPower << " W\n"
             << "# GPU Clock:             " << avgGpuFreq << " MHz\n"
             << "# CPU Utilization:       " << avgCpuUtil << " %\n"
             << "# CPU Temperature:       " << avgCpuTemp << " C\n"
             << "# CPU Power:             " << avgCpuPower << " W\n"
             << "# ========================================================\n";

    // 2. Also write companion _summary.txt file
    QString summaryPath = currentCapturePath_;
    if (summaryPath.endsWith(".csv", Qt::CaseInsensitive)) {
        summaryPath.chop(4);
    }
    summaryPath += "_summary.txt";
    std::ofstream sumFile(summaryPath.toStdString());
    if (sumFile.is_open()) {
        sumFile << "========================================================\n"
                << "GNUMON BENCHMARK SUMMARY\n"
                << "========================================================\n"
                << "Application:             " << appName << "\n"
                << "Process ID:              " << trackedPid_ << "\n"
                << "Date / Time:             " << dateTimeStr << "\n"
                << "Benchmark Duration:      " << std::fixed << std::setprecision(2) << durationSec << " s\n"
                << "Total Recorded Frames:   " << totalFrames << "\n\n"
                << "--------------------------------------------------------\n"
                << "FRAME RATE (FPS)\n"
                << "--------------------------------------------------------\n"
                << "Average FPS:             " << avgFps << " FPS\n"
                << "1% Low FPS:              " << onePercentLowFps << " FPS\n"
                << "0.1% Low FPS:            " << pointOnePercentLowFps << " FPS\n"
                << "Minimum FPS:             " << minFps << " FPS\n"
                << "Maximum FPS:             " << maxFps << " FPS\n\n"
                << "--------------------------------------------------------\n"
                << "FRAME TIME (ms)\n"
                << "--------------------------------------------------------\n"
                << "Average Frame Time:      " << avgFt << " ms\n"
                << "99th Percentile:         " << p99Ft << " ms\n"
                << "99.9th Percentile:       " << p999Ft << " ms\n"
                << "Minimum Frame Time:      " << minFt << " ms\n"
                << "Maximum Frame Time:      " << maxFt << " ms\n\n"
                << "--------------------------------------------------------\n"
                << "HARDWARE TELEMETRY (AVERAGES)\n"
                << "--------------------------------------------------------\n"
                << "GPU Utilization:         " << avgGpuUtil << " %\n"
                << "GPU Temperature:         " << avgGpuTemp << " C\n"
                << "GPU Power:               " << avgGpuPower << " W\n"
                << "GPU Clock:               " << avgGpuFreq << " MHz\n"
                << "CPU Utilization:         " << avgCpuUtil << " %\n"
                << "CPU Temperature:         " << avgCpuTemp << " C\n"
                << "CPU Power:               " << avgCpuPower << " W\n"
                << "========================================================\n";
        sumFile.close();
    }
}

QString MainWindow::GetCapturesDirectory() const {
    if (!config_.captureDirectory.isEmpty()) {
        QDir().mkpath(config_.captureDirectory);
        return config_.captureDirectory;
    }
    QString docs = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    if (docs.isEmpty()) {
        docs = QDir::homePath() + "/Documents";
    }
    QString dir = docs + "/gnumon/captures";
    QDir().mkpath(dir);
    return dir;
}

void MainWindow::OnOpenCapturesFolder() {
    LaunchHostFileManager(GetCapturesDirectory());
}

void MainWindow::keyPressEvent(QKeyEvent *event) {
    // Check overlay toggle
    if (event->key() == Qt::Key_F9 ||
        (event->key() == Qt::Key_O && (event->modifiers() & Qt::ControlModifier) && (event->modifiers() & Qt::ShiftModifier))) {
        OnToggleOverlay();
        event->accept();
        return;
    }

    // Check recording toggle
    if (event->key() == Qt::Key_F10 ||
        (event->key() == Qt::Key_K && (event->modifiers() & Qt::ControlModifier) && (event->modifiers() & Qt::ShiftModifier))) {
        OnToggleRecording();
        event->accept();
        return;
    }

    // Check preset cycle
    if (event->key() == Qt::Key_F8 || event->key() == Qt::Key_F11 ||
        (event->key() == Qt::Key_P && (event->modifiers() & Qt::ControlModifier) && (event->modifiers() & Qt::ShiftModifier))) {
        OnCyclePreset();
        event->accept();
        return;
    }

    // Back to main on Escape if in Settings or Loadout
    if (event->key() == Qt::Key_Escape && rootStack_->currentIndex() != 0) {
        OnBackToMain();
        event->accept();
        return;
    }

    QMainWindow::keyPressEvent(event);
}

} // namespace gnumon::gui
