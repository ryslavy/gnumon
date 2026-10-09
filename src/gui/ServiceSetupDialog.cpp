#include "ServiceSetupDialog.h"
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QMessageBox>
#include <QStandardPaths>

namespace gnumon::gui {

static QString FindSetupScript() {
    QString appDir = QCoreApplication::applicationDirPath();
    QString envAppDir = qgetenv("APPDIR");
    QStringList candidates = {
        appDir + "/setup-service.sh",
        envAppDir + "/usr/bin/setup-service.sh",
        appDir + "/../scripts/setup-service.sh",
        appDir + "/../../scripts/setup-service.sh",
        QDir::homePath() + "/.local/bin/setup-service.sh",
        "/usr/local/bin/setup-service.sh",
        "/usr/bin/setup-service.sh"
    };
    for (const auto& path : candidates) {
        if (!path.isEmpty() && QFileInfo::exists(path)) {
            return QFileInfo(path).canonicalFilePath();
        }
    }
    return "setup-service.sh";
}

ServiceSetupDialog::ServiceSetupDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle("gnumon Service & System Setup");
    setMinimumWidth(560);
    SetupUi();

    refreshTimer_ = new QTimer(this);
    connect(refreshTimer_, &QTimer::timeout, this, &ServiceSetupDialog::RefreshStatus);
    refreshTimer_->start(3000); // refresh every 3 seconds

    RefreshStatus();
}

void ServiceSetupDialog::SetupUi() {
    auto mainLayout = new QVBoxLayout(this);
    mainLayout->setSpacing(16);
    mainLayout->setContentsMargins(20, 20, 20, 20);

    // Title / Description
    auto lblHeader = new QLabel("<h3 style='color:#00e5ff; margin-bottom: 2px;'>System Service & Hardware Access Setup</h3>"
                                "<p style='color:#a0aab8; font-size:12px; margin-top: 0px;'>"
                                "Configure background coordination, global input privileges for Wayland, and Vulkan layer hooks."
                                "</p>", this);
    lblHeader->setTextFormat(Qt::RichText);
    mainLayout->addWidget(lblHeader);

    // 1. Daemon Service Card
    auto groupDaemon = new QGroupBox("1. Background Coordinator Daemon (gnumond)", this);
    auto daemonLayout = new QVBoxLayout(groupDaemon);
    daemonLayout->setSpacing(10);

    lblDaemonStatus_ = new QLabel("Status: Checking...", this);
    lblDaemonStatus_->setStyleSheet("font-weight: bold; font-size: 13px;");
    daemonLayout->addWidget(lblDaemonStatus_);

    auto lblDaemonDesc = new QLabel("Coordinates shared telemetry ring buffers, tracks active games, and manages CSV recording.", this);
    lblDaemonDesc->setStyleSheet("color: #8fa3b8; font-size: 11px;");
    daemonLayout->addWidget(lblDaemonDesc);

    auto daemonBtnLayout = new QHBoxLayout();
    btnStartService_ = new QPushButton("Start Daemon", this);
    btnStopService_ = new QPushButton("Stop Daemon", this);
    btnEnableService_ = new QPushButton("Enable Auto-start at Login", this);

    btnStartService_->setStyleSheet("QPushButton { background-color: #007799; color: white; padding: 6px 14px; border-radius: 4px; font-weight: bold; } QPushButton:hover { background-color: #0099bb; }");
    btnStopService_->setStyleSheet("QPushButton { background-color: #444a55; color: white; padding: 6px 14px; border-radius: 4px; } QPushButton:hover { background-color: #555c69; }");
    btnEnableService_->setStyleSheet("QPushButton { background-color: #00b060; color: white; padding: 6px 14px; border-radius: 4px; font-weight: bold; } QPushButton:hover { background-color: #00cc70; }");

    connect(btnStartService_, &QPushButton::clicked, this, &ServiceSetupDialog::OnStartService);
    connect(btnStopService_, &QPushButton::clicked, this, &ServiceSetupDialog::OnStopService);
    connect(btnEnableService_, &QPushButton::clicked, this, &ServiceSetupDialog::OnEnableService);

    daemonBtnLayout->addWidget(btnStartService_);
    daemonBtnLayout->addWidget(btnStopService_);
    daemonBtnLayout->addWidget(btnEnableService_);
    daemonLayout->addLayout(daemonBtnLayout);

    mainLayout->addWidget(groupDaemon);

    // 2. Hardware Input Permissions Card (udev)
    auto groupPerms = new QGroupBox("2. Hardware Input Access (evdev / Wayland)", this);
    auto permsLayout = new QVBoxLayout(groupPerms);
    permsLayout->setSpacing(10);

    lblUdevStatus_ = new QLabel("udev Rules: Checking...", this);
    lblUdevStatus_->setStyleSheet("font-weight: bold; font-size: 13px;");
    permsLayout->addWidget(lblUdevStatus_);

    lblInputAccess_ = new QLabel("Direct Input Access: Checking...", this);
    permsLayout->addWidget(lblInputAccess_);

    auto lblPermsDesc = new QLabel("Required for Mouse Click-to-Photon latency and global hotkeys (F8/F9/F10) under pure Wayland and Gamescope without window focus.", this);
    lblPermsDesc->setWordWrap(true);
    lblPermsDesc->setStyleSheet("color: #8fa3b8; font-size: 11px;");
    permsLayout->addWidget(lblPermsDesc);

    auto permsBtnLayout = new QHBoxLayout();
    btnInstallUdev_ = new QPushButton("Grant Hardware Input Permissions (pkexec)", this);
    btnInstallUdev_->setStyleSheet("QPushButton { background-color: #d97706; color: white; padding: 7px 16px; border-radius: 4px; font-weight: bold; } QPushButton:hover { background-color: #f59e0b; }");
    connect(btnInstallUdev_, &QPushButton::clicked, this, &ServiceSetupDialog::OnInstallUdevRules);

    permsBtnLayout->addWidget(btnInstallUdev_);
    permsBtnLayout->addStretch();
    permsLayout->addLayout(permsBtnLayout);

    mainLayout->addWidget(groupPerms);

    // 3. Vulkan Layers Integration Card
    auto groupLayers = new QGroupBox("3. Vulkan Capture Layers (64-bit & 32-bit Multilib)", this);
    auto layersLayout = new QVBoxLayout(groupLayers);
    layersLayout->setSpacing(10);

    lblVulkan64Status_ = new QLabel("64-bit Vulkan Layer: Checking...", this);
    lblVulkan64Status_->setStyleSheet("font-weight: bold; font-size: 13px;");
    layersLayout->addWidget(lblVulkan64Status_);

    lblVulkan32Status_ = new QLabel("32-bit Multilib Layer (Proton / Wine): Checking...", this);
    lblVulkan32Status_->setStyleSheet("font-weight: bold; font-size: 13px;");
    layersLayout->addWidget(lblVulkan32Status_);

    auto lblLayersDesc = new QLabel("Injects frametime measurement and in-game HUD into Vulkan swapchains for native games, Steam, and Flatpak Steam.", this);
    lblLayersDesc->setWordWrap(true);
    lblLayersDesc->setStyleSheet("color: #8fa3b8; font-size: 11px;");
    layersLayout->addWidget(lblLayersDesc);

    auto layersBtnLayout = new QHBoxLayout();
    btnInstallLayers_ = new QPushButton("Install / Refresh Vulkan Layers", this);
    btnInstallLayers_->setStyleSheet("QPushButton { background-color: #007799; color: white; padding: 6px 14px; border-radius: 4px; font-weight: bold; } QPushButton:hover { background-color: #0099bb; }");
    connect(btnInstallLayers_, &QPushButton::clicked, this, &ServiceSetupDialog::OnInstallLayers);

    layersBtnLayout->addWidget(btnInstallLayers_);
    layersBtnLayout->addStretch();
    layersLayout->addLayout(layersBtnLayout);

    mainLayout->addWidget(groupLayers);

    // Feedback message
    lblFeedback_ = new QLabel("", this);
    lblFeedback_->setStyleSheet("color: #00e5ff; font-weight: bold; font-size: 12px;");
    mainLayout->addWidget(lblFeedback_);

    // Bottom close button
    auto btnClose = new QPushButton("Close", this);
    btnClose->setStyleSheet("QPushButton { background-color: #333a46; color: white; padding: 6px 20px; border-radius: 4px; } QPushButton:hover { background-color: #444e5d; }");
    connect(btnClose, &QPushButton::clicked, this, &QDialog::accept);

    auto bottomLayout = new QHBoxLayout();
    bottomLayout->addStretch();
    bottomLayout->addWidget(btnClose);
    mainLayout->addLayout(bottomLayout);
}

QString ServiceSetupDialog::RunSetupCommand(const QString& action) {
    QString scriptPath = FindSetupScript();
    QProcess process;
    process.setProcessChannelMode(QProcess::MergedChannels);

    if (QFileInfo::exists(scriptPath)) {
        process.start("/bin/bash", {scriptPath, action});
    } else {
        process.start(scriptPath, {action});
    }

    int timeoutMs = (action.startsWith("install")) ? 60000 : 10000;
    if (!process.waitForFinished(timeoutMs)) {
        process.kill();
        return QString("Command timed out: %1").arg(action);
    }
    return QString::fromUtf8(process.readAll());
}

void ServiceSetupDialog::RefreshStatus() {
    QString output = RunSetupCommand("status");
    QMap<QString, QString> statusMap;
    const auto lines = output.split('\n', Qt::SkipEmptyParts);
    for (const auto& line : lines) {
        int eq = line.indexOf('=');
        if (eq > 0) {
            statusMap.insert(line.left(eq).trimmed(), line.mid(eq + 1).trimmed());
        }
    }
    UpdateUiFromStatus(statusMap);
}

void ServiceSetupDialog::UpdateUiFromStatus(const QMap<QString, QString>& status) {
    bool userActive = (status.value("user_service_active") == "active");
    bool sysActive = (status.value("system_service_active") == "active");
    bool daemonRunning = userActive || sysActive;
    bool userEnabled = (status.value("user_service_enabled") == "enabled");

    if (daemonRunning) {
        lblDaemonStatus_->setText(QString("Status: <span style='color:#00ff88;'>● Active / Running (%1)</span>")
                                      .arg(userActive ? "User service" : "System service"));
        btnStartService_->setEnabled(false);
        btnStopService_->setEnabled(true);
    } else {
        lblDaemonStatus_->setText("Status: <span style='color:#ff5555;'>○ Inactive / Stopped</span>");
        btnStartService_->setEnabled(true);
        btnStopService_->setEnabled(false);
    }

    if (userEnabled) {
        btnEnableService_->setText("Auto-start Enabled");
        btnEnableService_->setEnabled(false);
    } else {
        btnEnableService_->setText("Enable Auto-start at Login");
        btnEnableService_->setEnabled(true);
    }

    bool udevInstalled = (status.value("udev_rules_installed") == "yes");
    bool inputAccessible = (status.value("input_accessible") == "yes");

    if (udevInstalled) {
        lblUdevStatus_->setText("udev Rules: <span style='color:#00ff88;'>● Installed (/etc/udev/rules.d/99-gnumon-input.rules)</span>");
        btnInstallUdev_->setText("Re-apply Permissions (pkexec)");
    } else {
        lblUdevStatus_->setText("udev Rules: <span style='color:#ffaa00;'>▲ Not Installed</span>");
        btnInstallUdev_->setText("Grant Hardware Input Permissions (pkexec)");
    }

    if (inputAccessible) {
        lblInputAccess_->setText("Input Devices: <span style='color:#00ff88;'>● Accessible (Mouse click & global hotkeys ready)</span>");
    } else {
        lblInputAccess_->setText("Input Devices: <span style='color:#ffaa00;'>▲ Restricted (Click 'Grant Hardware Input Permissions')</span>");
    }

    bool vk64 = (status.value("vulkan_64_installed") == "yes");
    bool vk32 = (status.value("vulkan_32_installed") == "yes");

    lblVulkan64Status_->setText(QString("64-bit Vulkan Layer: %1")
                                    .arg(vk64 ? "<span style='color:#00ff88;'>● Registered & Ready</span>"
                                              : "<span style='color:#ff5555;'>○ Missing</span>"));

    lblVulkan32Status_->setText(QString("32-bit Multilib Layer (Proton / Wine): %1")
                                    .arg(vk32 ? "<span style='color:#00ff88;'>● Registered & Ready</span>"
                                              : "<span style='color:#ffaa00;'>▲ Not Installed (Click 'Install / Refresh')</span>"));
}

void ServiceSetupDialog::OnStartService() {
    lblFeedback_->setText("Starting daemon service...");
    qApp->processEvents();
    QString out = RunSetupCommand("start-user");
    lblFeedback_->setText(out.trimmed().isEmpty() ? "Daemon service started." : out.trimmed());
    RefreshStatus();
}

void ServiceSetupDialog::OnStopService() {
    lblFeedback_->setText("Stopping daemon service...");
    qApp->processEvents();
    QString out = RunSetupCommand("stop-user");
    lblFeedback_->setText(out.trimmed().isEmpty() ? "Daemon service stopped." : out.trimmed());
    RefreshStatus();
}

void ServiceSetupDialog::OnEnableService() {
    lblFeedback_->setText("Enabling user daemon service...");
    qApp->processEvents();
    QString out = RunSetupCommand("enable-user");
    lblFeedback_->setText(out.trimmed().isEmpty() ? "Daemon service enabled." : out.trimmed());
    RefreshStatus();
}

void ServiceSetupDialog::OnInstallUdevRules() {
    lblFeedback_->setText("Requesting root permissions via pkexec to install udev rules...");
    qApp->processEvents();
    QString output = RunSetupCommand("install-udev");
    lblFeedback_->setText(output.trimmed().isEmpty() ? "Udev setup finished." : output.trimmed());
    RefreshStatus();
}

void ServiceSetupDialog::OnInstallLayers() {
    lblFeedback_->setText("Installing Vulkan layers (64-bit and 32-bit)...");
    qApp->processEvents();
    QString output = RunSetupCommand("install-layers");
    lblFeedback_->setText(output.trimmed().isEmpty() ? "Vulkan layers installed." : output.trimmed());
    RefreshStatus();
}

} // namespace gnumon::gui
