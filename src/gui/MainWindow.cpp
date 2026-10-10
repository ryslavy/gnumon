#include "MainWindow.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QDateTime>
#include <QDir>
#include <QStandardPaths>
#include <QDesktopServices>
#include <QUrl>
#include <QProcess>
#include <QIcon>
#include <QFileInfo>
#include <QKeySequence>
#include <QMessageBox>
#include <filesystem>
#include <iostream>
#include <iomanip>
#include "../common/ProcUtils.h"

namespace gnumon::gui {

struct FramePayload {
    uint32_t processId = 0;
    uint64_t swapChain = 0;
    int32_t runtime = 0;
    int32_t presentMode = 0;
    double cpuStartTime = 0.0;
    double frameTimeMs = 0.0;
    double displayedFps = 0.0;
    double inPresentApiMs = 0.0;
    double gpuTimeMs = 0.0;
    double gpuPower = 0.0;
    double gpuTemp = 0.0;
    double gpuUtil = 0.0;
    double cpuUtil = 0.0;
    double cpuPower = 0.0;
};

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
}

MainWindow::~MainWindow() {
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
    if (config_.autoTarget) {
        AutoTargetProcess();
        return;
    }

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
    isRecording_ = !isRecording_;
    if (isRecording_) {
        // Auto-acquire active game PID if trackedPid_ is 0
        bool trackedHasRing = (trackedPid_ > 0 && std::filesystem::exists("/dev/shm/gnumon_ring_" + std::to_string(trackedPid_)));
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

        QString filename = QString("gnumon_capture_%1.csv").arg(QDateTime::currentDateTime().toString("yyyyMMdd_hhmmss"));
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

        // Write CSV Header
        csvFile_ << "ProcessID,SwapChainAddress,Runtime,CPUStartTime,CPUPresentTimeMs,DisplayedFPS,"
                 << "InPresentAPIMs,GPUTimeMs,GPUPowerW,GPUTemperatureC,GPUUtilizationPercent,"
                 << "CPUUtilizationPercent,CPUPowerW\n";
        csvFile_.flush();

        std::vector<PM_QUERY_ELEMENT> frameElements = {
            { PM_METRIC_PROCESS_ID, PM_STAT_NONE, 0, 0, offsetof(FramePayload, processId), sizeof(uint32_t) },
            { PM_METRIC_SWAP_CHAIN_ADDRESS, PM_STAT_NONE, 0, 0, offsetof(FramePayload, swapChain), sizeof(uint64_t) },
            { PM_METRIC_PRESENT_RUNTIME, PM_STAT_NONE, 0, 0, offsetof(FramePayload, runtime), sizeof(int32_t) },
            { PM_METRIC_CPU_START_TIME, PM_STAT_NONE, 0, 0, offsetof(FramePayload, cpuStartTime), sizeof(double) },
            { PM_METRIC_CPU_FRAME_TIME, PM_STAT_NONE, 0, 0, offsetof(FramePayload, frameTimeMs), sizeof(double) },
            { PM_METRIC_DISPLAYED_FPS, PM_STAT_NONE, 0, 0, offsetof(FramePayload, displayedFps), sizeof(double) },
            { PM_METRIC_IN_PRESENT_API, PM_STAT_NONE, 0, 0, offsetof(FramePayload, inPresentApiMs), sizeof(double) },
            { PM_METRIC_GPU_TIME, PM_STAT_NONE, 0, 0, offsetof(FramePayload, gpuTimeMs), sizeof(double) },
            { PM_METRIC_GPU_POWER, PM_STAT_NONE, 0, 0, offsetof(FramePayload, gpuPower), sizeof(double) },
            { PM_METRIC_GPU_TEMPERATURE, PM_STAT_NONE, 0, 0, offsetof(FramePayload, gpuTemp), sizeof(double) },
            { PM_METRIC_GPU_UTILIZATION, PM_STAT_NONE, 0, 0, offsetof(FramePayload, gpuUtil), sizeof(double) },
            { PM_METRIC_CPU_UTILIZATION, PM_STAT_NONE, 0, 0, offsetof(FramePayload, cpuUtil), sizeof(double) },
            { PM_METRIC_CPU_POWER, PM_STAT_NONE, 0, 0, offsetof(FramePayload, cpuPower), sizeof(double) },
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
        if (csvFile_.is_open()) {
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
    // Process frame recording if active
    if (isRecording_ && frameQuery_ && csvFile_.is_open()) {
        // Check capture duration auto-stop
        if (config_.enableCaptureDuration && config_.captureDurationSeconds > 0) {
            uint64_t elapsedSec = (QDateTime::currentMSecsSinceEpoch() - recordingStartMs_) / 1000;
            if (elapsedSec >= static_cast<uint64_t>(config_.captureDurationSeconds)) {
                OnToggleRecording();
                return;
            }
        }

        constexpr uint32_t BATCH_SIZE = 128;
        std::vector<uint8_t> buffer(BATCH_SIZE * frameBlobSize_);
        uint32_t numFrames = BATCH_SIZE;

        while (pmConsumeFrames(frameQuery_, trackedPid_, buffer.data(), &numFrames) == PM_STATUS_SUCCESS && numFrames > 0) {
            for (uint32_t i = 0; i < numFrames; ++i) {
                auto* frame = reinterpret_cast<FramePayload*>(buffer.data() + (i * frameBlobSize_));
                csvFile_ << frame->processId << ","
                         << "0x" << std::hex << frame->swapChain << std::dec << ","
                         << "Vulkan,"
                         << std::fixed << std::setprecision(3)
                         << frame->cpuStartTime << ","
                         << frame->frameTimeMs << ","
                         << frame->displayedFps << ","
                         << frame->inPresentApiMs << ","
                         << frame->gpuTimeMs << ","
                         << frame->gpuPower << ","
                         << frame->gpuTemp << ","
                         << frame->gpuUtil << ","
                         << frame->cpuUtil << ","
                         << frame->cpuPower << "\n";
                recordedFramesCount_++;
            }
            if (numFrames < BATCH_SIZE) break;
            numFrames = BATCH_SIZE;
        }
        csvFile_.flush();
    }

    UpdateStatusBar();
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
    QString dir = GetCapturesDirectory();
    QDir().mkpath(dir);

    // Host desktop file managers (Dolphin, Nautilus, etc.) crash if they inherit
    // AppImage's bundled Qt6 libraries and LD_LIBRARY_PATH.
    // Launch detached xdg-open with sanitized host environment.
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    QString ld = env.value("LD_LIBRARY_PATH");
    QString appDir = env.value("APPDIR");
    if (!ld.isEmpty()) {
        QStringList kept;
        for (const QString& part : ld.split(':')) {
            if (!appDir.isEmpty() && part.startsWith(appDir)) continue;
            if (part.contains(".mount_") || part.contains("build-appimage/AppDir")) continue;
            kept << part;
        }
        if (kept.isEmpty()) {
            env.remove("LD_LIBRARY_PATH");
        } else {
            env.insert("LD_LIBRARY_PATH", kept.join(':'));
        }
    }
    env.remove("QT_PLUGIN_PATH");

    QProcess proc;
    proc.setProcessEnvironment(env);
    proc.startDetached("/usr/bin/xdg-open", {dir}, dir);
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
