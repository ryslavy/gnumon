#include "MainWindow.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QMessageBox>
#include <QDateTime>
#include <QDir>
#include <QStandardPaths>
#include <QDesktopServices>
#include <QUrl>
#include <QProcess>
#include <filesystem>
#include <vector>
#include <fstream>
#include <iostream>
#include "../common/ProcUtils.h"

namespace gnumon::gui {

struct QueryPayload {
    double gpuPower = 0.0;
    double gpuTemp = 0.0;
    double gpuUtil = 0.0;
    double gpuFreq = 0.0;
    uint64_t gpuVramUsed = 0;
    uint64_t gpuVramTotal = 0;
    double cpuUtil = 0.0;
    double cpuPower = 0.0;
    double cpuTemp = 0.0;
    double cpuFreq = 0.0;
    double displayedFps = 0.0;
    double frameTimeMs = 0.0;
    double gpuTimeMs = 0.0;
};

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
    setWindowTitle("gnumon — PresentMon Linux");
    resize(820, 680);

    SetupUi();

    // Initialize PresentMon session
    if (pmOpenSession(&session_) == PM_STATUS_SUCCESS) {
        std::vector<PM_QUERY_ELEMENT> elements = {
            { PM_METRIC_GPU_POWER, PM_STAT_NONE, 0, 0, offsetof(QueryPayload, gpuPower), sizeof(double) },
            { PM_METRIC_GPU_TEMPERATURE, PM_STAT_NONE, 0, 0, offsetof(QueryPayload, gpuTemp), sizeof(double) },
            { PM_METRIC_GPU_UTILIZATION, PM_STAT_NONE, 0, 0, offsetof(QueryPayload, gpuUtil), sizeof(double) },
            { PM_METRIC_GPU_FREQUENCY, PM_STAT_NONE, 0, 0, offsetof(QueryPayload, gpuFreq), sizeof(double) },
            { PM_METRIC_GPU_MEM_USED, PM_STAT_NONE, 0, 0, offsetof(QueryPayload, gpuVramUsed), sizeof(uint64_t) },
            { PM_METRIC_GPU_MEM_SIZE, PM_STAT_NONE, 0, 0, offsetof(QueryPayload, gpuVramTotal), sizeof(uint64_t) },

            { PM_METRIC_CPU_UTILIZATION, PM_STAT_NONE, 0, 0, offsetof(QueryPayload, cpuUtil), sizeof(double) },
            { PM_METRIC_CPU_POWER, PM_STAT_NONE, 0, 0, offsetof(QueryPayload, cpuPower), sizeof(double) },
            { PM_METRIC_CPU_TEMPERATURE, PM_STAT_NONE, 0, 0, offsetof(QueryPayload, cpuTemp), sizeof(double) },
            { PM_METRIC_CPU_FREQUENCY, PM_STAT_NONE, 0, 0, offsetof(QueryPayload, cpuFreq), sizeof(double) },

            { PM_METRIC_DISPLAYED_FPS, PM_STAT_AVG, 0, 0, offsetof(QueryPayload, displayedFps), sizeof(double) },
            { PM_METRIC_CPU_FRAME_TIME, PM_STAT_NONE, 0, 0, offsetof(QueryPayload, frameTimeMs), sizeof(double) },
            { PM_METRIC_GPU_TIME, PM_STAT_NONE, 0, 0, offsetof(QueryPayload, gpuTimeMs), sizeof(double) },
        };

        pmRegisterDynamicQuery(session_, &query_, elements.data(), elements.size(), 1000.0, 0.0);

        // Read static device names
        char gpuNameBuf[128]{};
        PM_QUERY_ELEMENT gpuNameElem{ PM_METRIC_GPU_NAME, PM_STAT_NONE, 1, 0, 0, sizeof(gpuNameBuf) };
        if (pmPollStaticQuery(session_, &gpuNameElem, 0, reinterpret_cast<uint8_t*>(gpuNameBuf)) == PM_STATUS_SUCCESS) {
            lblGpuName_->setText(QString("GPU: %1").arg(gpuNameBuf));
        }

        char cpuNameBuf[128]{};
        PM_QUERY_ELEMENT cpuNameElem{ PM_METRIC_CPU_NAME, PM_STAT_NONE, 0, 0, 0, sizeof(cpuNameBuf) };
        if (pmPollStaticQuery(session_, &cpuNameElem, 0, reinterpret_cast<uint8_t*>(cpuNameBuf)) == PM_STATUS_SUCCESS) {
            lblCpuName_->setText(QString("CPU: %1").arg(cpuNameBuf));
        }
    } else {
        lblStatus_->setText("Status: Error opening PresentMon session");
    }

    PopulateProcessList();

    if (session_) {
        overlay_ = new PresentMonOverlay(session_);
        if (trackedPid_ > 0) {
            overlay_->SetTargetProcess(trackedPid_, comboProcess_->currentText().toStdString());
        }
        overlay_->show();
    }

    pollTimer_ = new QTimer(this);
    connect(pollTimer_, &QTimer::timeout, this, &MainWindow::OnPollTimer);
    pollTimer_->start(100); // 10 Hz refresh for responsive frametime graph
}

MainWindow::~MainWindow() {
    if (overlay_) {
        delete overlay_;
        overlay_ = nullptr;
    }
    if (isRecording_) {
        OnToggleRecording();
    }
    if (frameQuery_) {
        pmFreeFrameQuery(frameQuery_);
    }
    if (query_) {
        pmFreeDynamicQuery(query_);
    }
    if (session_) {
        pmCloseSession(session_);
    }
}

void MainWindow::SetupUi() {
    auto *centralWidget = new QWidget(this);
    setCentralWidget(centralWidget);
    auto *mainLayout = new QVBoxLayout(centralWidget);
    mainLayout->setSpacing(12);

    // --- Top Control Toolbar ---
    topContainer_ = new QWidget(this);
    auto *topLayout = new QHBoxLayout(topContainer_);
    topLayout->setContentsMargins(0, 0, 0, 0);

    auto *lblProc = new QLabel("Target Process:", this);
    lblProc->setStyleSheet("font-weight: bold;");
    comboProcess_ = new QComboBox(this);
    comboProcess_->setMinimumWidth(240);
    connect(comboProcess_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &MainWindow::OnProcessChanged);

    btnRefreshProcess_ = new QPushButton("Refresh", this);
    connect(btnRefreshProcess_, &QPushButton::clicked, this, &MainWindow::OnRefreshProcesses);

    btnConfigMetrics_ = new QPushButton("Metrics...", this);
    connect(btnConfigMetrics_, &QPushButton::clicked, this, &MainWindow::OnConfigureMetrics);

    const char* homeDir = std::getenv("HOME");
    bool layerInstalled = false;
    if (homeDir) {
        layerInstalled = std::filesystem::exists(std::filesystem::path(homeDir) / ".local/share/vulkan/implicit_layer.d/VkLayer_gnumon.json") &&
                         std::filesystem::exists(std::filesystem::path(homeDir) / ".local/lib/gnumon/libVkLayer_gnumon.so");
    }
    btnInstallLayer_ = new QPushButton(layerInstalled ? "Uninstall Layer" : "Install Layer", this);
    if (layerInstalled) {
        btnInstallLayer_->setStyleSheet("background-color: #37474f; color: #ffb74d;");
    }
    connect(btnInstallLayer_, &QPushButton::clicked, this, &MainWindow::OnToggleLayerInstall);

    btnOverlay_ = new QPushButton("Overlay (F11)", this);
    connect(btnOverlay_, &QPushButton::clicked, this, &MainWindow::OnToggleOverlay);

    btnMiniOverlay_ = new QPushButton("Mini HUD (F12)", this);
    connect(btnMiniOverlay_, &QPushButton::clicked, this, &MainWindow::OnToggleMiniOverlay);

    btnOpenCaptures_ = new QPushButton("Captures", this);
    btnOpenCaptures_->setToolTip("Open captures folder in file manager");
    connect(btnOpenCaptures_, &QPushButton::clicked, this, &MainWindow::OnOpenCapturesFolder);

    btnRecord_ = new QPushButton("Start Capture (CSV)", this);
    btnRecord_->setStyleSheet("padding: 6px 16px; font-weight: bold; background-color: #2e7d32; color: white; border-radius: 4px;");
    connect(btnRecord_, &QPushButton::clicked, this, &MainWindow::OnToggleRecording);

    topLayout->addWidget(lblProc);
    topLayout->addWidget(comboProcess_);
    topLayout->addWidget(btnRefreshProcess_);
    topLayout->addWidget(btnConfigMetrics_);
    topLayout->addWidget(btnInstallLayer_);
    topLayout->addWidget(btnOverlay_);
    topLayout->addWidget(btnMiniOverlay_);
    topLayout->addStretch();
    topLayout->addWidget(btnOpenCaptures_);
    topLayout->addWidget(btnRecord_);

    mainLayout->addWidget(topContainer_);

    // --- Frametime Graph Widget ---
    auto *graphGroup = new QGroupBox("Real-Time Frametime & FPS History", this);
    graphGroup_ = graphGroup;
    auto *graphLayout = new QVBoxLayout(graphGroup);
    graphWidget_ = new FrametimeGraphWidget(this);
    graphLayout->addWidget(graphWidget_);
    mainLayout->addWidget(graphGroup);

    // --- GPU Group ---
    auto *gpuGroup = new QGroupBox("GPU Telemetry", this);
    gpuGroup_ = gpuGroup;
    auto *gpuLayout = new QGridLayout(gpuGroup);

    lblGpuName_ = new QLabel("GPU: Detecting...", this);
    lblGpuName_->setStyleSheet("font-weight: bold; color: #4fc3f7;");
    lblGpuPower_ = new QLabel("Power: 0.0 W", this);
    lblGpuTemp_ = new QLabel("Temperature: 0.0 °C", this);
    lblGpuFreq_ = new QLabel("Clock: 0.0 MHz", this);
    lblGpuUtil_ = new QLabel("Utilization: 0.0 %", this);
    lblGpuVram_ = new QLabel("VRAM: 0 / 0 MB", this);

    barGpuUtil_ = new QProgressBar(this);
    barGpuUtil_->setRange(0, 100);
    barGpuUtil_->setValue(0);
    barGpuUtil_->setTextVisible(false);

    gpuLayout->addWidget(lblGpuName_, 0, 0, 1, 3);
    gpuLayout->addWidget(lblGpuPower_, 1, 0);
    gpuLayout->addWidget(lblGpuTemp_, 1, 1);
    gpuLayout->addWidget(lblGpuFreq_, 1, 2);
    gpuLayout->addWidget(lblGpuUtil_, 2, 0);
    gpuLayout->addWidget(lblGpuVram_, 2, 1);
    gpuLayout->addWidget(barGpuUtil_, 3, 0, 1, 3);

    mainLayout->addWidget(gpuGroup);

    // --- CPU Group ---
    auto *cpuGroup = new QGroupBox("CPU Telemetry", this);
    cpuGroup_ = cpuGroup;
    auto *cpuLayout = new QGridLayout(cpuGroup);

    lblCpuName_ = new QLabel("CPU: Detecting...", this);
    lblCpuName_->setStyleSheet("font-weight: bold; color: #81c784;");
    lblCpuPower_ = new QLabel("Package Power: 0.0 W", this);
    lblCpuTemp_ = new QLabel("Temperature: 0.0 °C", this);
    lblCpuFreq_ = new QLabel("Clock: 0.0 MHz", this);
    lblCpuUtil_ = new QLabel("Utilization: 0.0 %", this);

    barCpuUtil_ = new QProgressBar(this);
    barCpuUtil_->setRange(0, 100);
    barCpuUtil_->setValue(0);
    barCpuUtil_->setTextVisible(false);

    cpuLayout->addWidget(lblCpuName_, 0, 0, 1, 3);
    cpuLayout->addWidget(lblCpuPower_, 1, 0);
    cpuLayout->addWidget(lblCpuTemp_, 1, 1);
    cpuLayout->addWidget(lblCpuFreq_, 1, 2);
    cpuLayout->addWidget(lblCpuUtil_, 2, 0);
    cpuLayout->addWidget(barCpuUtil_, 3, 0, 1, 3);

    mainLayout->addWidget(cpuGroup);

    // --- Status Bar ---
    lblStatus_ = new QLabel("Status: Monitoring idle", this);
    lblStatus_->setStyleSheet("color: #90a4ae;");
    mainLayout->addWidget(lblStatus_);

    ApplyMetricsConfig();
}

void MainWindow::OnConfigureMetrics() {
    MetricsConfigDialog dlg(metricsConfig_, this);
    if (dlg.exec() == QDialog::Accepted) {
        metricsConfig_ = dlg.GetConfig();
        ApplyMetricsConfig();
    }
}

void MainWindow::ApplyMetricsConfig() {
    if (graphGroup_) graphGroup_->setVisible(metricsConfig_.showGraph);

    if (lblGpuPower_) lblGpuPower_->setVisible(metricsConfig_.showGpuPower);
    if (lblGpuTemp_) lblGpuTemp_->setVisible(metricsConfig_.showGpuTemp);
    if (lblGpuFreq_) lblGpuFreq_->setVisible(metricsConfig_.showGpuFreq);
    if (lblGpuUtil_) lblGpuUtil_->setVisible(metricsConfig_.showGpuUtil);
    if (barGpuUtil_) barGpuUtil_->setVisible(metricsConfig_.showGpuUtil);
    if (lblGpuVram_) lblGpuVram_->setVisible(metricsConfig_.showGpuVram);

    if (lblCpuPower_) lblCpuPower_->setVisible(metricsConfig_.showCpuPower);
    if (lblCpuTemp_) lblCpuTemp_->setVisible(metricsConfig_.showCpuTemp);
    if (lblCpuFreq_) lblCpuFreq_->setVisible(metricsConfig_.showCpuFreq);
    if (lblCpuUtil_) lblCpuUtil_->setVisible(metricsConfig_.showCpuUtil);
    if (barCpuUtil_) barCpuUtil_->setVisible(metricsConfig_.showCpuUtil);

    bool anyGpu = metricsConfig_.showGpuPower || metricsConfig_.showGpuTemp ||
                  metricsConfig_.showGpuFreq || metricsConfig_.showGpuUtil || metricsConfig_.showGpuVram;
    if (gpuGroup_) gpuGroup_->setVisible(anyGpu);

    bool anyCpu = metricsConfig_.showCpuPower || metricsConfig_.showCpuTemp ||
                  metricsConfig_.showCpuFreq || metricsConfig_.showCpuUtil;
    if (cpuGroup_) cpuGroup_->setVisible(anyCpu);
}

void MainWindow::OnToggleLayerInstall() {
    const char* home = std::getenv("HOME");
    if (!home) return;
    std::filesystem::path homePath(home);
    std::filesystem::path destLibDir = homePath / ".local/lib/gnumon";
    std::filesystem::path destBinDir = homePath / ".local/bin";
    std::filesystem::path destImpDir = homePath / ".local/share/vulkan/implicit_layer.d";
    std::filesystem::path destExpDir = homePath / ".local/share/vulkan/explicit_layer.d";

    std::filesystem::path impFile = destImpDir / "VkLayer_gnumon.json";
    std::filesystem::path expFile = destExpDir / "VkLayer_gnumon.json";
    std::filesystem::path installedVkLib = destLibDir / "libVkLayer_gnumon.so";

    bool isInstalled = std::filesystem::exists(impFile) && std::filesystem::exists(installedVkLib);

    if (isInstalled) {
        // Uninstall
        std::error_code ec;
        std::filesystem::remove(impFile, ec);
        std::filesystem::remove(expFile, ec);
        btnInstallLayer_->setText("Install Layer");
        btnInstallLayer_->setStyleSheet("");
        lblStatus_->setText("Status: Vulkan Layer uninstalled from ~/.local/share/vulkan");
        return;
    }

    // Install
    std::error_code ec;
    std::filesystem::create_directories(destLibDir, ec);
    std::filesystem::create_directories(destBinDir, ec);
    std::filesystem::create_directories(destImpDir, ec);
    std::filesystem::create_directories(destExpDir, ec);

    std::filesystem::path exeDir = std::filesystem::canonical("/proc/self/exe", ec).parent_path();

    // 1. Locate and copy libVkLayer_gnumon.so
    std::vector<std::filesystem::path> vkCandidates = {
        exeDir / "libVkLayer_gnumon.so",
        exeDir / "../lib/libVkLayer_gnumon.so",
        exeDir / "../lib64/libVkLayer_gnumon.so",
        exeDir / "build-container/libVkLayer_gnumon.so",
        exeDir / "../build-container/libVkLayer_gnumon.so",
        exeDir / "build-host/libVkLayer_gnumon.so",
        exeDir / "../build-host/libVkLayer_gnumon.so",
        installedVkLib
    };

    std::filesystem::path srcVkLib;
    for (const auto& cand : vkCandidates) {
        if (std::filesystem::exists(cand) && !std::filesystem::equivalent(cand, installedVkLib, ec)) {
            srcVkLib = cand;
            break;
        }
    }

    if (!srcVkLib.empty()) {
        std::filesystem::copy_file(srcVkLib, installedVkLib, std::filesystem::copy_options::overwrite_existing, ec);
    }

    // 2. Locate and copy libgnumon_gl.so
    std::filesystem::path installedGlLib = destLibDir / "libgnumon_gl.so";
    std::vector<std::filesystem::path> glCandidates = {
        exeDir / "libgnumon_gl.so",
        exeDir / "../lib/libgnumon_gl.so",
        exeDir / "../lib64/libgnumon_gl.so",
        exeDir / "../build-container/libgnumon_gl.so",
        exeDir / "../build-host/libgnumon_gl.so"
    };
    for (const auto& cand : glCandidates) {
        if (std::filesystem::exists(cand) && !std::filesystem::equivalent(cand, installedGlLib, ec)) {
            std::filesystem::copy_file(cand, installedGlLib, std::filesystem::copy_options::overwrite_existing, ec);
            break;
        }
    }

    // 3. Locate and copy gnumon-run
    std::filesystem::path installedRun = destBinDir / "gnumon-run";
    std::vector<std::filesystem::path> runCandidates = {
        exeDir / "gnumon-run",
        exeDir / "../bin/gnumon-run",
        exeDir / "scripts/gnumon-run",
        exeDir / "../scripts/gnumon-run"
    };
    for (const auto& cand : runCandidates) {
        if (std::filesystem::exists(cand) && !std::filesystem::equivalent(cand, installedRun, ec)) {
            std::filesystem::copy_file(cand, installedRun, std::filesystem::copy_options::overwrite_existing, ec);
            std::filesystem::permissions(installedRun,
                std::filesystem::perms::owner_all | std::filesystem::perms::group_read | std::filesystem::perms::group_exec | std::filesystem::perms::others_read | std::filesystem::perms::others_exec,
                std::filesystem::perm_options::replace, ec);
            break;
        }
    }

    // 4. Generate JSON manifests
    {
        std::ofstream imp(impFile);
        imp << "{\n"
            << "    \"file_format_version\" : \"1.0.0\",\n"
            << "    \"layer\" : {\n"
            << "        \"name\": \"VK_LAYER_GNUMON_capture\",\n"
            << "        \"type\": \"GLOBAL\",\n"
            << "        \"library_path\": \"" << installedVkLib.string() << "\",\n"
            << "        \"api_version\": \"1.3.0\",\n"
            << "        \"implementation_version\": \"1\",\n"
            << "        \"description\": \"gnumon Linux PresentMon frame capture layer\",\n"
            << "        \"functions\": {\n"
            << "            \"vkNegotiateLoaderLayerInterfaceVersion\": \"vkNegotiateLoaderLayerInterfaceVersion\"\n"
            << "        },\n"
            << "        \"enable_environment\": {\n"
            << "            \"ENABLE_GNUMON\": \"1\"\n"
            << "        },\n"
            << "        \"disable_environment\": {\n"
            << "            \"DISABLE_GNUMON\": \"1\"\n"
            << "        }\n"
            << "    }\n"
            << "}\n";
    }

    {
        std::ofstream exp(expFile);
        exp << "{\n"
            << "    \"file_format_version\" : \"1.0.0\",\n"
            << "    \"layer\" : {\n"
            << "        \"name\": \"VK_LAYER_GNUMON_capture\",\n"
            << "        \"type\": \"GLOBAL\",\n"
            << "        \"library_path\": \"" << installedVkLib.string() << "\",\n"
            << "        \"api_version\": \"1.3.0\",\n"
            << "        \"implementation_version\": \"1\",\n"
            << "        \"description\": \"gnumon Linux PresentMon frame capture layer\",\n"
            << "        \"functions\": {\n"
            << "            \"vkNegotiateLoaderLayerInterfaceVersion\": \"vkNegotiateLoaderLayerInterfaceVersion\"\n"
            << "        }\n"
            << "    }\n"
            << "}\n";
    }

    btnInstallLayer_->setText("Uninstall Layer");
    btnInstallLayer_->setStyleSheet("background-color: #37474f; color: #ffb74d;");
    lblStatus_->setText("Status: Vulkan Layer installed to ~/.local/share/vulkan and gnumon-run ready!");
}

void MainWindow::PopulateProcessList() {
    comboProcess_->blockSignals(true);
    comboProcess_->clear();

    comboProcess_->addItem("Auto-detect Active Game (PID 0)", 0);

    // Clean stale ring buffers from terminated games
    common::CleanStaleRings();

    // 1. Prioritize all live games currently active with gnumon
    auto activePids = common::GetActiveRingPids();
    int activeIndex = -1;
    for (uint32_t activePid : activePids) {
        std::string commPath = "/proc/" + std::to_string(activePid) + "/comm";
        std::ifstream commFile(commPath);
        std::string comm = "Game";
        if (commFile.is_open()) {
            std::getline(commFile, comm);
        }
        comboProcess_->addItem(QString("🟢 %1 (PID %2) [ACTIVE GAME / Vulkan]").arg(QString::fromStdString(comm)).arg(activePid), activePid);
        if (activeIndex < 0) {
            activeIndex = comboProcess_->count() - 1;
        }
    }

    // Scan /proc for other running processes
    for (const auto& entry : std::filesystem::directory_iterator("/proc")) {
        if (!entry.is_directory()) continue;
        std::string filename = entry.path().filename().string();
        if (filename.empty() || !std::isdigit(filename[0])) continue;

        uint32_t pid = std::stoul(filename);
        // Skip already added active pids
        if (std::find(activePids.begin(), activePids.end(), pid) != activePids.end()) continue;

        std::string commPath = entry.path() / "comm";
        std::ifstream commFile(commPath);
        if (commFile.is_open()) {
            std::string comm;
            std::getline(commFile, comm);
            if (!comm.empty()) {
                // Prioritize GUI / graphics processes or common games
                if (comm == "vkcube" || comm == "steam" || comm == "gamescope" ||
                    comm.find("game") != std::string::npos || comm.find("wine") != std::string::npos) {
                    comboProcess_->addItem(QString("%1 (PID %2) [3D/Vulkan]").arg(QString::fromStdString(comm)).arg(pid), pid);
                } else if (pid > 1000) {
                    comboProcess_->addItem(QString("%1 (PID %2)").arg(QString::fromStdString(comm)).arg(pid), pid);
                }
            }
        }
    }

    if (activeIndex >= 1 && trackedPid_ == 0) {
        comboProcess_->setCurrentIndex(activeIndex);
        trackedPid_ = comboProcess_->itemData(activeIndex).toUInt();
        if (session_) {
            pmStartTrackingProcess(session_, trackedPid_);
        }
        if (overlay_) {
            overlay_->SetTargetProcess(trackedPid_, comboProcess_->itemText(activeIndex).toStdString());
        }
    }

    comboProcess_->blockSignals(false);
}

void MainWindow::OnRefreshProcesses() {
    PopulateProcessList();
}

void MainWindow::OnProcessChanged(int index) {
    if (index < 0) return;
    uint32_t pid = comboProcess_->itemData(index).toUInt();
    uint32_t tgid = common::GetTgidForPid(pid);
    trackedPid_ = (tgid > 0) ? tgid : pid;
    if (session_) {
        pmStartTrackingProcess(session_, trackedPid_);
        graphWidget_->Clear();
    }
    if (overlay_) {
        overlay_->SetTargetProcess(trackedPid_, comboProcess_->itemText(index).toStdString());
    }
}

void MainWindow::OnPollTimer() {
    if (session_) {
        bool overlayHotkey = false;
        if (pmCheckOverlayHotkeyTriggered(session_, &overlayHotkey) == PM_STATUS_SUCCESS && overlayHotkey) {
            OnToggleOverlay();
        }
        bool recordHotkey = false;
        if (pmCheckRecordHotkeyTriggered(session_, &recordHotkey) == PM_STATUS_SUCCESS && recordHotkey) {
            OnToggleRecording();
        }
    }

    if (!query_) return;

    QueryPayload data{};
    uint32_t numSwapChains = 0;
    if (pmPollDynamicQuery(query_, trackedPid_, reinterpret_cast<uint8_t*>(&data), &numSwapChains) == PM_STATUS_SUCCESS) {
        lblGpuPower_->setText(QString("Power: %1 W").arg(data.gpuPower, 0, 'f', 1));
        lblGpuTemp_->setText(QString("Temperature: %1 °C").arg(data.gpuTemp, 0, 'f', 1));
        lblGpuFreq_->setText(QString("Clock: %1 MHz").arg(data.gpuFreq, 0, 'f', 0));
        lblGpuUtil_->setText(QString("Utilization: %1 %").arg(data.gpuUtil, 0, 'f', 1));
        barGpuUtil_->setValue(static_cast<int>(data.gpuUtil));

        double vramUsedMb = static_cast<double>(data.gpuVramUsed) / (1024.0 * 1024.0);
        double vramTotalMb = static_cast<double>(data.gpuVramTotal) / (1024.0 * 1024.0);
        lblGpuVram_->setText(QString("VRAM: %1 / %2 MB").arg(vramUsedMb, 0, 'f', 0).arg(vramTotalMb, 0, 'f', 0));

        lblCpuPower_->setText(QString("Package Power: %1 W").arg(data.cpuPower, 0, 'f', 1));
        lblCpuTemp_->setText(QString("Temperature: %1 °C").arg(data.cpuTemp, 0, 'f', 1));
        lblCpuFreq_->setText(QString("Clock: %1 MHz").arg(data.cpuFreq, 0, 'f', 0));
        lblCpuUtil_->setText(QString("Utilization: %1 %").arg(data.cpuUtil, 0, 'f', 1));
        barCpuUtil_->setValue(static_cast<int>(data.cpuUtil));

        // Update real-time Frametime & FPS graph
        if (data.frameTimeMs > 0.0) {
            double fps = (data.displayedFps > 0.0) ? data.displayedFps : (1000.0 / data.frameTimeMs);
            graphWidget_->AddSample(data.frameTimeMs, fps);
        }
    }

    // Process frame recording if active
    if (isRecording_ && frameQuery_ && csvFile_.is_open()) {
        constexpr uint32_t BATCH_SIZE = 128;
        std::vector<uint8_t> buffer(BATCH_SIZE * frameBlobSize_);
        uint32_t numFrames = BATCH_SIZE;

        while (pmConsumeFrames(frameQuery_, trackedPid_, buffer.data(), &numFrames) == PM_STATUS_SUCCESS && numFrames > 0) {
            for (uint32_t i = 0; i < numFrames; ++i) {
                auto* frame = reinterpret_cast<FramePayload*>(buffer.data() + (i * frameBlobSize_));
                csvFile_ << frame->processId << ","
                         << "0x" << std::hex << frame->swapChain << std::dec << ","
                         << "Vulkan,"
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

        lblStatus_->setText(QString("Status: Recording... %1 frames captured").arg(recordedFramesCount_));
    }
}

QString MainWindow::GetCapturesDirectory() const {
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
    env.remove("QT_QPA_PLATFORM_PLUGIN_PATH");

    QProcess proc;
    proc.setProcessEnvironment(env);
    proc.setProgram("xdg-open");
    proc.setArguments(QStringList() << dir);
    if (!proc.startDetached()) {
        QDesktopServices::openUrl(QUrl::fromLocalFile(dir));
    }
}

void MainWindow::OnToggleRecording() {
    isRecording_ = !isRecording_;
    if (isRecording_) {
        QString filename = QString("gnumon_capture_%1.csv").arg(QDateTime::currentDateTime().toString("yyyyMMdd_hhmmss"));
        currentCapturePath_ = GetCapturesDirectory() + "/" + filename;
        csvFile_.open(currentCapturePath_.toStdString());
        if (!csvFile_.is_open()) {
            QMessageBox::critical(this, "Capture Error", QString("Could not create CSV file at:\n%1").arg(currentCapturePath_));
            isRecording_ = false;
            return;
        }

        // Write CSV header
        csvFile_ << "ProcessID,SwapChainAddress,Runtime,CPUStartTime,CPUPresentTimeMs,DisplayedFPS,"
                 << "InPresentAPIMs,GPUTimeMs,GPUPowerW,GPUTemperatureC,GPUUtilizationPercent,"
                 << "CPUUtilizationPercent,CPUPowerW\n";

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
        btnRecord_->setText("Stop Capture");
        btnRecord_->setStyleSheet("padding: 6px 16px; font-weight: bold; background-color: #c62828; color: white; border-radius: 4px;");
        lblStatus_->setText(QString("Status: Recording to %1...").arg(currentCapturePath_));
    } else {
        if (csvFile_.is_open()) {
            csvFile_.close();
        }
        if (frameQuery_) {
            pmFreeFrameQuery(frameQuery_);
            frameQuery_ = nullptr;
        }

        btnRecord_->setText("Start Capture (CSV)");
        btnRecord_->setStyleSheet("padding: 6px 16px; font-weight: bold; background-color: #2e7d32; color: white; border-radius: 4px;");
        lblStatus_->setText(QString("Status: Capture finished (%1 frames saved to %2)").arg(recordedFramesCount_).arg(currentCapturePath_));
    }

    if (overlay_) {
        overlay_->SetRecordingState(isRecording_);
    }
}

void MainWindow::OnToggleOverlay() {
    if (overlay_) {
        overlay_->ToggleVisibility();
    }
}

void MainWindow::OnToggleMiniOverlay() {
    isMiniOverlay_ = !isMiniOverlay_;
    if (isMiniOverlay_) {
        setWindowFlags(Qt::Window | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
        setAttribute(Qt::WA_TranslucentBackground, true);
        setStyleSheet("QMainWindow { background-color: rgba(20, 24, 30, 220); border: 2px solid #00bcd4; border-radius: 8px; }");
        topContainer_->setVisible(false);
        resize(420, 280);
        show();
    } else {
        setWindowFlags(Qt::Window);
        setAttribute(Qt::WA_TranslucentBackground, false);
        setStyleSheet("");
        topContainer_->setVisible(true);
        resize(820, 680);
        show();
    }
}

void MainWindow::keyPressEvent(QKeyEvent *event) {
    if (event->key() == Qt::Key_F11) {
        OnToggleOverlay();
        event->accept();
        return;
    }
    if (event->key() == Qt::Key_F10) {
        OnToggleRecording();
        event->accept();
        return;
    }
    if (event->key() == Qt::Key_F12 || (event->key() == Qt::Key_O && (event->modifiers() & Qt::ControlModifier))) {
        OnToggleMiniOverlay();
        event->accept();
        return;
    }
    if (event->key() == Qt::Key_Escape && isMiniOverlay_) {
        OnToggleMiniOverlay();
        event->accept();
        return;
    }
    QMainWindow::keyPressEvent(event);
}

void MainWindow::mousePressEvent(QMouseEvent *event) {
    if (isMiniOverlay_ && event->button() == Qt::LeftButton) {
        dragPosition_ = event->globalPosition().toPoint() - frameGeometry().topLeft();
        event->accept();
        return;
    }
    QMainWindow::mousePressEvent(event);
}

void MainWindow::mouseMoveEvent(QMouseEvent *event) {
    if (isMiniOverlay_ && (event->buttons() & Qt::LeftButton)) {
        move(event->globalPosition().toPoint() - dragPosition_);
        event->accept();
        return;
    }
    QMainWindow::mouseMoveEvent(event);
}

} // namespace gnumon::gui
