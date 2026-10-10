#include "WindowedOverlayWidget.h"
#include <QPainterPath>
#include <QFontDatabase>
#include <QWindow>
#include <cmath>
#include <algorithm>
#include "../../include/gnumon/PresentMonAPI.h"

namespace gnumon::gui {

WindowedOverlayWidget::WindowedOverlayWidget(AppConfig *config, QWidget *parent)
    : QWidget(parent, Qt::Window | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint),
      config_(config)
{
    setAttribute(Qt::WA_TranslucentBackground, true);
    setWindowTitle("gnumon Overlay (Windowed)");

    ReloadLayout();
}

void WindowedOverlayWidget::ReloadLayout() {
    int w = (config_->overlayWidth >= 250) ? config_->overlayWidth : 440;

    int totalH = 36; // Header
    for (const auto& widget : config_->loadout.widgets) {
        if (widget.widgetType == WidgetType::Graph) {
            totalH += (widget.metrics.size() * 18) + 76 + 24;
        } else {
            totalH += 24;
        }
    }
    totalH += 12;

    resize(w, totalH);
    if (pos().x() <= 0 && pos().y() <= 0) {
        move(60, 60);
    }
    update();
}

void WindowedOverlayWidget::PushSample(std::deque<float>& q, float val, size_t maxLen) {
    q.push_back(val);
    if (q.size() > maxLen) {
        q.pop_front();
    }
}

void WindowedOverlayWidget::UpdateMetrics(double fps, double displayedFps, double frameTimeMs, double untilDisplayedMs,
                                          double dropped, double animErrorMs, const ipc::TelemetrySnapshot* telem)
{
    if (frameTimeMs > 0.0) {
        PushSample(frametimes_, static_cast<float>(frameTimeMs));
    }
    PushSample(histUntilDisplayed_, static_cast<float>(untilDisplayedMs));
    PushSample(histFps_, static_cast<float>(displayedFps > 0.0 ? displayedFps : fps));
    PushSample(histAnimError_, static_cast<float>(animErrorMs));
    PushSample(histDroppedFrames_, static_cast<float>(dropped));

    if (telem && telem->valid != 0) {
        PushSample(histGpuUtil_, telem->gpuUtil);
        PushSample(histGpuPower_, telem->gpuPower);
        PushSample(histGpuTemp_, telem->gpuTemp);
        PushSample(histGpuFreq_, telem->gpuFreq);
        PushSample(histGpuVoltage_, telem->gpuVoltage);
        PushSample(histGpuFanSpeed_, telem->gpuFanSpeed);
        PushSample(histVramUsed_, telem->vramUsedGb);
        PushSample(histCpuUtil_, telem->cpuUtil);
        PushSample(histCpuPower_, telem->cpuPower);
        PushSample(histCpuTemp_, telem->cpuTemp);
        PushSample(histCpuFreq_, telem->cpuFreq);

        if (telem->gpuName[0] != '\0') {
            gpuName_ = QString::fromUtf8(telem->gpuName);
            if (gpuName_.contains("AMD", Qt::CaseInsensitive) || gpuName_.contains("Radeon", Qt::CaseInsensitive)) {
                gpuVendor_ = "AMD";
            } else if (gpuName_.contains("Intel", Qt::CaseInsensitive) || gpuName_.contains("Arc", Qt::CaseInsensitive)) {
                gpuVendor_ = "Intel";
            } else if (gpuName_.contains("NVIDIA", Qt::CaseInsensitive) || gpuName_.contains("GeForce", Qt::CaseInsensitive)) {
                gpuVendor_ = "NVIDIA";
            }
        }
        if (telem->cpuName[0] != '\0') {
            cpuName_ = QString::fromUtf8(telem->cpuName);
            if (cpuName_.contains("AMD", Qt::CaseInsensitive) || cpuName_.contains("Ryzen", Qt::CaseInsensitive)) {
                cpuVendor_ = "AMD";
            } else if (cpuName_.contains("Intel", Qt::CaseInsensitive) || cpuName_.contains("Core", Qt::CaseInsensitive)) {
                cpuVendor_ = "Intel";
            }
        }
    }

    update();
}

void WindowedOverlayWidget::UpdateFromSnapshot(const PM_FULL_TELEMETRY_SNAPSHOT& snap) {
    if (snap.presentedFrameTimeMs > 0.0) {
        PushSample(frametimes_, static_cast<float>(snap.presentedFrameTimeMs));
    } else if (snap.presentFps > 0.0) {
        PushSample(frametimes_, static_cast<float>(1000.0 / snap.presentFps));
    }
    PushSample(histUntilDisplayed_, static_cast<float>(snap.untilDisplayedMs));
    PushSample(histFps_, static_cast<float>(snap.displayedFps > 0.0 ? snap.displayedFps : snap.presentFps));
    PushSample(histAnimError_, static_cast<float>(snap.animationErrorMs));
    PushSample(histDroppedFrames_, static_cast<float>(snap.droppedFrames));

    PushSample(histGpuTime_, static_cast<float>(snap.gpuTimeMs));
    PushSample(histGpuBusy_, static_cast<float>(snap.gpuBusyMs));
    PushSample(histGpuWait_, static_cast<float>(snap.gpuWaitMs));
    PushSample(histCpuBusy_, static_cast<float>(snap.cpuBusyMs));
    PushSample(histCpuWait_, static_cast<float>(snap.cpuWaitMs));

    PushSample(histGpuUtil_, static_cast<float>(snap.gpuUtilizationPercent));
    PushSample(histGpuPower_, static_cast<float>(snap.gpuPowerWatts));
    PushSample(histGpuVoltage_, static_cast<float>(snap.gpuVoltageMv));
    PushSample(histGpuTemp_, static_cast<float>(snap.gpuTemperatureEdgeC));
    PushSample(histGpuFreq_, static_cast<float>(snap.gpuFrequencyMhz));
    PushSample(histGpuFanSpeed_, static_cast<float>(snap.gpuFanSpeedRpm));
    PushSample(histVramUsed_, static_cast<float>(snap.vramUsedBytes / (1024.0 * 1024.0 * 1024.0)));

    PushSample(histCpuUtil_, static_cast<float>(snap.cpuUtilizationPercent));
    PushSample(histCpuPower_, static_cast<float>(snap.cpuPackagePowerWatts));
    PushSample(histCpuTemp_, static_cast<float>(snap.cpuTemperatureC));
    PushSample(histCpuFreq_, static_cast<float>(snap.cpuFrequencyMhz));

    if (snap.gpuName[0] != '\0') {
        gpuName_ = QString::fromUtf8(snap.gpuName);
    }
    if (snap.cpuName[0] != '\0') {
        cpuName_ = QString::fromUtf8(snap.cpuName);
    }
    if (snap.processName[0] != '\0') {
        appName_ = QString::fromUtf8(snap.processName);
    }

    switch (snap.gpuVendor) {
        case PM_DEVICE_VENDOR_AMD: gpuVendor_ = "AMD"; break;
        case PM_DEVICE_VENDOR_INTEL: gpuVendor_ = "Intel"; break;
        case PM_DEVICE_VENDOR_NVIDIA: gpuVendor_ = "NVIDIA"; break;
        default:
            if (gpuName_.contains("AMD", Qt::CaseInsensitive) || gpuName_.contains("Radeon", Qt::CaseInsensitive)) gpuVendor_ = "AMD";
            else if (gpuName_.contains("Intel", Qt::CaseInsensitive) || gpuName_.contains("Arc", Qt::CaseInsensitive)) gpuVendor_ = "Intel";
            else if (gpuName_.contains("NVIDIA", Qt::CaseInsensitive) || gpuName_.contains("GeForce", Qt::CaseInsensitive)) gpuVendor_ = "NVIDIA";
            break;
    }

    switch (snap.cpuVendor) {
        case PM_DEVICE_VENDOR_AMD: cpuVendor_ = "AMD"; break;
        case PM_DEVICE_VENDOR_INTEL: cpuVendor_ = "Intel"; break;
        default:
            if (cpuName_.contains("AMD", Qt::CaseInsensitive) || cpuName_.contains("Ryzen", Qt::CaseInsensitive)) cpuVendor_ = "AMD";
            else if (cpuName_.contains("Intel", Qt::CaseInsensitive) || cpuName_.contains("Core", Qt::CaseInsensitive)) cpuVendor_ = "Intel";
            break;
    }

    update();
}

const std::deque<float>& WindowedOverlayWidget::GetHistoryForMetric(int metricId) {
    switch (metricId) {
        case PM_METRIC_BETWEEN_DISPLAY_CHANGE:
        case PM_METRIC_DISPLAYED_FRAME_TIME:
        case PM_METRIC_PRESENTED_FRAME_TIME:
        case PM_METRIC_BETWEEN_PRESENTS:
        case PM_METRIC_BETWEEN_APP_START:
        case PM_METRIC_BETWEEN_SIMULATION_START:
        case PM_METRIC_CPU_FRAME_TIME:
        case PM_METRIC_IN_PRESENT_API:
        case PM_METRIC_FLIP_DELAY:
            return frametimes_;
        case PM_METRIC_UNTIL_DISPLAYED:
        case PM_METRIC_DISPLAY_LATENCY:
        case PM_METRIC_CLICK_TO_PHOTON_LATENCY:
        case PM_METRIC_ALL_INPUT_TO_PHOTON_LATENCY:
        case PM_METRIC_INSTRUMENTED_LATENCY:
        case PM_METRIC_PC_LATENCY:
        case PM_METRIC_GPU_LATENCY:
        case PM_METRIC_RENDER_PRESENT_LATENCY:
            return histUntilDisplayed_;
        case PM_METRIC_DROPPED_FRAMES:
            return histDroppedFrames_;
        case PM_METRIC_APPLICATION_FPS:
        case PM_METRIC_DISPLAYED_FPS:
        case PM_METRIC_PRESENTED_FPS:
            return histFps_;
        case PM_METRIC_ANIMATION_ERROR:
        case PM_METRIC_ANIMATION_TIME:
            return histAnimError_;
        case PM_METRIC_GPU_TIME:
            return histGpuTime_;
        case PM_METRIC_GPU_BUSY:
            return histGpuBusy_;
        case PM_METRIC_GPU_WAIT:
            return histGpuWait_;
        case PM_METRIC_CPU_BUSY:
            return histCpuBusy_;
        case PM_METRIC_CPU_WAIT:
            return histCpuWait_;
        case PM_METRIC_GPU_UTILIZATION:
        case PM_METRIC_GPU_RENDER_COMPUTE_UTILIZATION:
        case PM_METRIC_GPU_MEDIA_UTILIZATION:
            return histGpuUtil_;
        case PM_METRIC_GPU_POWER:
        case PM_METRIC_GPU_CARD_POWER:
        case PM_METRIC_GPU_SUSTAINED_POWER_LIMIT:
            return histGpuPower_;
        case PM_METRIC_GPU_VOLTAGE:
            return histGpuVoltage_;
        case PM_METRIC_GPU_TEMPERATURE:
        case PM_METRIC_GPU_VOLTAGE_REGULATOR_TEMPERATURE:
        case PM_METRIC_GPU_MEM_TEMPERATURE:
            return histGpuTemp_;
        case PM_METRIC_GPU_FREQUENCY:
        case PM_METRIC_GPU_EFFECTIVE_FREQUENCY:
        case PM_METRIC_GPU_MEM_FREQUENCY:
        case PM_METRIC_GPU_MEM_EFFECTIVE_FREQUENCY:
            return histGpuFreq_;
        case PM_METRIC_GPU_FAN_SPEED:
            return histGpuFanSpeed_;
        case PM_METRIC_GPU_MEM_USED:
        case PM_METRIC_GPU_MEM_SIZE:
        case PM_METRIC_GPU_MEM_UTILIZATION:
            return histVramUsed_;
        case PM_METRIC_CPU_UTILIZATION:
        case PM_METRIC_CPU_CORE_UTILITY:
            return histCpuUtil_;
        case PM_METRIC_CPU_POWER:
        case PM_METRIC_CPU_POWER_LIMIT:
            return histCpuPower_;
        case PM_METRIC_CPU_TEMPERATURE:
        case PM_METRIC_CPU_CORE_TEMPERATURE:
            return histCpuTemp_;
        case PM_METRIC_CPU_FREQUENCY:
            return histCpuFreq_;
        default:
            return frametimes_;
    }
}

float WindowedOverlayWidget::CalculateStat(const std::deque<float>& q, int statId) {
    if (q.empty()) return 0.0f;
    if (statId == 4 || statId == 0) return q.back(); // Raw
    if (statId == 1) { // Avg
        double sum = 0.0;
        for (float v : q) sum += v;
        return static_cast<float>(sum / q.size());
    }
    if (statId == 2) { // Min
        float m = q.front();
        for (float v : q) if (v < m) m = v;
        return m;
    }
    if (statId == 3) { // Max
        float m = q.front();
        for (float v : q) if (v > m) m = v;
        return m;
    }
    if (statId == 6 || statId == 5) { // 99% / 1%
        std::vector<float> sorted(q.begin(), q.end());
        std::sort(sorted.begin(), sorted.end());
        double pct = (statId == 6) ? 0.99 : 0.01;
        size_t idx = static_cast<size_t>(std::floor(sorted.size() * pct));
        return sorted[std::min(idx, sorted.size() - 1)];
    }
    return q.back();
}

bool WindowedOverlayWidget::IsStringMetric(int metricId) const {
    return (metricId == PM_METRIC_GPU_NAME || metricId == PM_METRIC_CPU_NAME ||
            metricId == PM_METRIC_GPU_VENDOR || metricId == PM_METRIC_CPU_VENDOR ||
            metricId == PM_METRIC_PRESENT_RUNTIME || metricId == PM_METRIC_PRESENT_MODE ||
            metricId == PM_METRIC_FRAME_TYPE || metricId == PM_METRIC_APPLICATION);
}

QString WindowedOverlayWidget::GetStringMetricValue(int metricId) const {
    switch (metricId) {
        case PM_METRIC_GPU_NAME: return gpuName_.isEmpty() ? "AMD Radeon RX 6600 XT" : gpuName_;
        case PM_METRIC_CPU_NAME: return cpuName_.isEmpty() ? "AMD Ryzen CPU" : cpuName_;
        case PM_METRIC_GPU_VENDOR: return gpuVendor_;
        case PM_METRIC_CPU_VENDOR: return cpuVendor_;
        case PM_METRIC_APPLICATION: return appName_;
        case PM_METRIC_PRESENT_RUNTIME: return "Vulkan/OpenGL";
        case PM_METRIC_PRESENT_MODE: return "Composed Flip";
        case PM_METRIC_FRAME_TYPE: return "Application";
        default: return "";
    }
}

static QString GetMetricLabel(int metricId) {
    switch (metricId) {
        case PM_METRIC_APPLICATION: return "Application";
        case PM_METRIC_SWAP_CHAIN_ADDRESS: return "Swap Chain";
        case PM_METRIC_GPU_VENDOR: return "GPU Vendor";
        case PM_METRIC_GPU_NAME: return "GPU Name";
        case PM_METRIC_CPU_VENDOR: return "CPU Vendor";
        case PM_METRIC_CPU_NAME: return "CPU Name";
        case PM_METRIC_CPU_START_TIME: return "CPU Start Time";
        case PM_METRIC_CPU_START_QPC: return "CPU Start QPC";
        case PM_METRIC_CPU_FRAME_TIME: return "CPU Frame Time";
        case PM_METRIC_CPU_BUSY: return "CPU Busy";
        case PM_METRIC_CPU_WAIT: return "CPU Wait";
        case PM_METRIC_DISPLAYED_FPS: return "Displayed FPS";
        case PM_METRIC_PRESENTED_FPS: return "Presented FPS";
        case PM_METRIC_GPU_TIME: return "GPU Time";
        case PM_METRIC_GPU_BUSY: return "GPU Busy";
        case PM_METRIC_GPU_WAIT: return "GPU Wait";
        case PM_METRIC_DROPPED_FRAMES: return "Dropped Frames";
        case PM_METRIC_DISPLAYED_TIME: return "Displayed Time";
        case PM_METRIC_SYNC_INTERVAL: return "Sync Interval";
        case PM_METRIC_PRESENT_FLAGS: return "Present Flags";
        case PM_METRIC_PRESENT_MODE: return "Present Mode";
        case PM_METRIC_PRESENT_RUNTIME: return "Present Runtime";
        case PM_METRIC_ALLOWS_TEARING: return "Allows Tearing";
        case PM_METRIC_GPU_LATENCY: return "GPU Latency";
        case PM_METRIC_DISPLAY_LATENCY: return "Display Latency";
        case PM_METRIC_CLICK_TO_PHOTON_LATENCY: return "Click to Photon Latency";
        case PM_METRIC_GPU_SUSTAINED_POWER_LIMIT: return "GPU Sustained Power Limit";
        case PM_METRIC_GPU_POWER: return "GPU Power";
        case PM_METRIC_GPU_VOLTAGE: return "GPU Voltage";
        case PM_METRIC_GPU_FREQUENCY: return "GPU Frequency";
        case PM_METRIC_GPU_TEMPERATURE: return "GPU Temperature";
        case PM_METRIC_GPU_FAN_SPEED: return "GPU Fan Speed";
        case PM_METRIC_GPU_UTILIZATION: return "GPU Utilization";
        case PM_METRIC_GPU_RENDER_COMPUTE_UTILIZATION: return "GPU Render/Compute";
        case PM_METRIC_GPU_MEDIA_UTILIZATION: return "GPU Media Util";
        case PM_METRIC_GPU_POWER_LIMITED: return "GPU Power Limited";
        case PM_METRIC_GPU_TEMPERATURE_LIMITED: return "GPU Temp Limited";
        case PM_METRIC_GPU_CURRENT_LIMITED: return "GPU Current Limited";
        case PM_METRIC_GPU_VOLTAGE_LIMITED: return "GPU Voltage Limited";
        case PM_METRIC_GPU_UTILIZATION_LIMITED: return "GPU Util Limited";
        case PM_METRIC_GPU_MEM_POWER: return "GPU VRAM Power";
        case PM_METRIC_GPU_MEM_VOLTAGE: return "GPU VRAM Voltage";
        case PM_METRIC_GPU_MEM_FREQUENCY: return "GPU VRAM Frequency";
        case PM_METRIC_GPU_MEM_EFFECTIVE_FREQUENCY: return "GPU VRAM Effective Freq";
        case PM_METRIC_GPU_MEM_TEMPERATURE: return "GPU VRAM Temp";
        case PM_METRIC_GPU_MEM_SIZE: return "GPU VRAM Total Size";
        case PM_METRIC_GPU_MEM_USED: return "GPU VRAM Used";
        case PM_METRIC_GPU_MEM_UTILIZATION: return "GPU VRAM Utilization";
        case PM_METRIC_GPU_MEM_MAX_BANDWIDTH: return "GPU VRAM Max Bandwidth";
        case PM_METRIC_GPU_MEM_WRITE_BANDWIDTH: return "GPU VRAM Write Bandwidth";
        case PM_METRIC_GPU_MEM_READ_BANDWIDTH: return "GPU VRAM Read Bandwidth";
        case PM_METRIC_GPU_MEM_POWER_LIMITED: return "GPU Mem Power Limited";
        case PM_METRIC_GPU_MEM_TEMPERATURE_LIMITED: return "GPU Mem Temp Limited";
        case PM_METRIC_GPU_MEM_CURRENT_LIMITED: return "GPU Mem Current Limited";
        case PM_METRIC_GPU_MEM_VOLTAGE_LIMITED: return "GPU Mem Voltage Limited";
        case PM_METRIC_GPU_MEM_UTILIZATION_LIMITED: return "GPU Mem Util Limited";
        case PM_METRIC_CPU_UTILIZATION: return "CPU Utilization";
        case PM_METRIC_CPU_POWER_LIMIT: return "CPU Power Limit";
        case PM_METRIC_CPU_POWER: return "CPU Power";
        case PM_METRIC_CPU_TEMPERATURE: return "CPU Temperature";
        case PM_METRIC_CPU_FREQUENCY: return "CPU Frequency";
        case PM_METRIC_CPU_CORE_UTILITY: return "CPU Core Utility";
        case PM_METRIC_APPLICATION_FPS: return "Application FPS";
        case PM_METRIC_FRAME_TYPE: return "Frame Type";
        case PM_METRIC_ANIMATION_ERROR: return "Animation Error";
        case PM_METRIC_ALL_INPUT_TO_PHOTON_LATENCY: return "All Input to Photon";
        case PM_METRIC_INSTRUMENTED_LATENCY: return "Instrumented Latency";
        case PM_METRIC_ANIMATION_TIME: return "Animation Time";
        case PM_METRIC_GPU_EFFECTIVE_FREQUENCY: return "GPU Effective Freq";
        case PM_METRIC_GPU_VOLTAGE_REGULATOR_TEMPERATURE: return "GPU VRM Temp";
        case PM_METRIC_GPU_MEM_EFFECTIVE_BANDWIDTH: return "GPU VRAM Eff Bandwidth";
        case PM_METRIC_GPU_OVERVOLTAGE_PERCENT: return "GPU Overvoltage %";
        case PM_METRIC_GPU_TEMPERATURE_PERCENT: return "GPU Temperature %";
        case PM_METRIC_GPU_POWER_PERCENT: return "GPU Power %";
        case PM_METRIC_GPU_FAN_SPEED_PERCENT: return "GPU Fan Speed %";
        case PM_METRIC_GPU_CARD_POWER: return "GPU Card Power";
        case PM_METRIC_PRESENT_START_TIME: return "Present Start Time";
        case PM_METRIC_PRESENT_START_QPC: return "Present Start QPC";
        case PM_METRIC_BETWEEN_PRESENTS: return "Between Presents";
        case PM_METRIC_IN_PRESENT_API: return "In Present API";
        case PM_METRIC_BETWEEN_DISPLAY_CHANGE: return "Between Display Change";
        case PM_METRIC_UNTIL_DISPLAYED: return "Until Displayed";
        case PM_METRIC_RENDER_PRESENT_LATENCY: return "Render Present Latency";
        case PM_METRIC_BETWEEN_SIMULATION_START: return "Between Sim Start";
        case PM_METRIC_PC_LATENCY: return "PC Latency";
        case PM_METRIC_DISPLAYED_FRAME_TIME: return "Displayed Frame Time";
        case PM_METRIC_BETWEEN_APP_START: return "Between App Start";
        case PM_METRIC_PRESENTED_FRAME_TIME: return "Presented Frame Time";
        case PM_METRIC_FLIP_DELAY: return "Flip Delay";
        case PM_METRIC_PSO_COMPILE_COUNT: return "PSO Compile Count";
        case PM_METRIC_PSO_COMPILE_TIME: return "PSO Compile Time";
        case PM_METRIC_PSO_COMPILE_BUSY_PERCENT: return "PSO Compile Busy %";
        case PM_METRIC_PROCESS_ID: return "Process ID";
        case PM_METRIC_SESSION_START_QPC: return "Session Start QPC";
        case PM_METRIC_CPU_CORE_TEMPERATURE: return "CPU Core Temp";
        default: return "Metric";
    }
}

static QString GetMetricUnits(int metricId) {
    switch (metricId) {
        case PM_METRIC_CPU_START_TIME:
        case PM_METRIC_CPU_FRAME_TIME:
        case PM_METRIC_CPU_BUSY:
        case PM_METRIC_CPU_WAIT:
        case PM_METRIC_GPU_TIME:
        case PM_METRIC_GPU_BUSY:
        case PM_METRIC_GPU_WAIT:
        case PM_METRIC_DISPLAYED_TIME:
        case PM_METRIC_GPU_LATENCY:
        case PM_METRIC_DISPLAY_LATENCY:
        case PM_METRIC_CLICK_TO_PHOTON_LATENCY:
        case PM_METRIC_ALL_INPUT_TO_PHOTON_LATENCY:
        case PM_METRIC_INSTRUMENTED_LATENCY:
        case PM_METRIC_ANIMATION_TIME:
        case PM_METRIC_PRESENT_START_TIME:
        case PM_METRIC_BETWEEN_PRESENTS:
        case PM_METRIC_IN_PRESENT_API:
        case PM_METRIC_BETWEEN_DISPLAY_CHANGE:
        case PM_METRIC_UNTIL_DISPLAYED:
        case PM_METRIC_RENDER_PRESENT_LATENCY:
        case PM_METRIC_BETWEEN_SIMULATION_START:
        case PM_METRIC_PC_LATENCY:
        case PM_METRIC_DISPLAYED_FRAME_TIME:
        case PM_METRIC_BETWEEN_APP_START:
        case PM_METRIC_PRESENTED_FRAME_TIME:
        case PM_METRIC_FLIP_DELAY:
        case PM_METRIC_ANIMATION_ERROR:
        case PM_METRIC_PSO_COMPILE_TIME:
            return "ms";
        case PM_METRIC_DISPLAYED_FPS:
        case PM_METRIC_PRESENTED_FPS:
        case PM_METRIC_APPLICATION_FPS:
            return "FPS";
        case PM_METRIC_GPU_UTILIZATION:
        case PM_METRIC_GPU_RENDER_COMPUTE_UTILIZATION:
        case PM_METRIC_GPU_MEDIA_UTILIZATION:
        case PM_METRIC_GPU_MEM_UTILIZATION:
        case PM_METRIC_CPU_UTILIZATION:
        case PM_METRIC_CPU_CORE_UTILITY:
        case PM_METRIC_GPU_OVERVOLTAGE_PERCENT:
        case PM_METRIC_GPU_TEMPERATURE_PERCENT:
        case PM_METRIC_GPU_POWER_PERCENT:
        case PM_METRIC_GPU_FAN_SPEED_PERCENT:
        case PM_METRIC_PSO_COMPILE_BUSY_PERCENT:
            return "%";
        case PM_METRIC_GPU_SUSTAINED_POWER_LIMIT:
        case PM_METRIC_GPU_POWER:
        case PM_METRIC_GPU_MEM_POWER:
        case PM_METRIC_CPU_POWER_LIMIT:
        case PM_METRIC_CPU_POWER:
        case PM_METRIC_GPU_CARD_POWER:
            return "W";
        case PM_METRIC_GPU_VOLTAGE:
        case PM_METRIC_GPU_MEM_VOLTAGE:
            return "mV";
        case PM_METRIC_GPU_FREQUENCY:
        case PM_METRIC_GPU_MEM_FREQUENCY:
        case PM_METRIC_GPU_MEM_EFFECTIVE_FREQUENCY:
        case PM_METRIC_CPU_FREQUENCY:
        case PM_METRIC_GPU_EFFECTIVE_FREQUENCY:
            return "MHz";
        case PM_METRIC_GPU_TEMPERATURE:
        case PM_METRIC_GPU_MEM_TEMPERATURE:
        case PM_METRIC_CPU_TEMPERATURE:
        case PM_METRIC_GPU_VOLTAGE_REGULATOR_TEMPERATURE:
        case PM_METRIC_CPU_CORE_TEMPERATURE:
            return "°C";
        case PM_METRIC_GPU_FAN_SPEED:
            return "RPM";
        case PM_METRIC_GPU_MEM_SIZE:
        case PM_METRIC_GPU_MEM_USED:
            return "GB";
        case PM_METRIC_GPU_MEM_MAX_BANDWIDTH:
        case PM_METRIC_GPU_MEM_WRITE_BANDWIDTH:
        case PM_METRIC_GPU_MEM_READ_BANDWIDTH:
        case PM_METRIC_GPU_MEM_EFFECTIVE_BANDWIDTH:
            return "GB/s";
        default:
            return "";
    }
}

void WindowedOverlayWidget::paintEvent(QPaintEvent *) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    int w = width();
    int h = height();

    // 1. Background Panel with glowing top border
    QColor bgColor(11, 14, 21, 235);
    QColor borderColor(0, 188, 242, 100);
    p.setPen(QPen(borderColor, 1.5));
    p.setBrush(bgColor);
    p.drawRoundedRect(1, 1, w - 2, h - 2, 6, 6);

    // Cyan top glow line
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(0, 229, 255, 230));
    p.drawRoundedRect(1, 1, w - 2, 2.5, 1, 1);

    // 2. Title Bar: "GNUMON PRESENTMON [WINDOWED]" + Close button
    p.setFont(QFont("sans-serif", 10, QFont::Bold));
    p.setPen(QColor(0, 230, 140));
    p.drawText(14, 24, "GNUMON");

    p.setFont(QFont("sans-serif", 9, QFont::Normal));
    p.setPen(QColor(160, 170, 190));
    p.drawText(76, 24, "PRESENTMON");

    p.setFont(QFont("sans-serif", 8, QFont::Bold));
    p.setPen(QColor(0, 200, 255));
    p.drawText(166, 24, "[WINDOWED]");

    // Close 'X' Button at top right
    p.setPen(QColor(160, 170, 190));
    p.setFont(QFont("sans-serif", 10, QFont::Bold));
    p.drawText(w - 22, 24, "✕");

    int curY = 40;

    // 3. Render Widgets
    for (const auto& widget : config_->loadout.widgets) {
        if (widget.widgetType == WidgetType::Readout) {
            if (widget.metrics.isEmpty()) continue;
            const auto& line = widget.metrics[0];
            bool isStr = IsStringMetric(line.metricId);

            // Color swatch
            p.setPen(Qt::NoPen);
            p.setBrush(line.lineColor);
            p.drawRect(14, curY + 3, 8, 8);

            // Label
            QString lbl;
            if (isStr) {
                lbl = GetMetricLabel(line.metricId);
            } else {
                QString statName = "";
                if (line.statId == 1) statName = "(avg)";
                else if (line.statId == 2) statName = "(min)";
                else if (line.statId == 3) statName = "(max)";
                else if (line.statId == 4) statName = "(raw)";
                else if (line.statId == 5) statName = "(1%)";
                else if (line.statId == 6) statName = "(99%)";

                lbl = statName.isEmpty() ? GetMetricLabel(line.metricId)
                                         : QString("%1 %2").arg(GetMetricLabel(line.metricId), statName);
            }
            p.setPen(QColor(220, 225, 235));
            p.setFont(QFont("sans-serif", 9, QFont::Normal));
            p.drawText(28, curY + 12, lbl);

            // Value + Unit
            QString valStr;
            if (isStr) {
                valStr = GetStringMetricValue(line.metricId);
            } else {
                const auto& hist = GetHistoryForMetric(line.metricId);
                float val = CalculateStat(hist, line.statId);
                QString units = GetMetricUnits(line.metricId);
                valStr = units.isEmpty() ? QString::number(val, 'f', 1)
                                         : QString("%1 %2").arg(QString::number(val, 'f', 1), units);
            }
            p.setPen(QColor(line.lineColor));
            p.setFont(QFont("sans-serif", 9, QFont::Bold));
            p.drawText(w - 14 - p.fontMetrics().horizontalAdvance(valStr), curY + 12, valStr);

            curY += 24;
        } else {
            // Graph Widget (Oscilloscope)
            for (int s = 0; s < widget.metrics.size(); ++s) {
                const auto& line = widget.metrics[s];
                bool isStr = IsStringMetric(line.metricId);

                p.setPen(Qt::NoPen);
                p.setBrush(line.lineColor);
                p.drawRect(14, curY + 4, 8, 8);

                QString lbl;
                if (isStr) {
                    lbl = GetMetricLabel(line.metricId);
                } else {
                    QString statName = "";
                    if (line.statId == 1) statName = "(avg)";
                    else if (line.statId == 2) statName = "(min)";
                    else if (line.statId == 3) statName = "(max)";
                    else if (line.statId == 4) statName = "(raw)";
                    else if (line.statId == 5) statName = "(1%)";
                    else if (line.statId == 6) statName = "(99%)";

                    lbl = statName.isEmpty() ? GetMetricLabel(line.metricId)
                                             : QString("%1 %2").arg(GetMetricLabel(line.metricId), statName);
                }
                p.setPen(QColor(220, 225, 235));
                p.setFont(QFont("sans-serif", 8, QFont::Normal));
                p.drawText(28, curY + 12, lbl);

                QString valStr;
                if (isStr) {
                    valStr = GetStringMetricValue(line.metricId);
                } else {
                    const auto& hist = GetHistoryForMetric(line.metricId);
                    float val = CalculateStat(hist, line.statId);
                    QString units = GetMetricUnits(line.metricId);
                    valStr = units.isEmpty() ? QString::number(val, 'f', 1)
                                             : QString("%1 %2").arg(QString::number(val, 'f', 1), units);
                }
                p.setPen(line.lineColor);
                p.setFont(QFont("sans-serif", 8, QFont::Bold));
                p.drawText(w - 14 - p.fontMetrics().horizontalAdvance(valStr), curY + 12, valStr);

                curY += 18;
            }

            // Graph Box
            int gx = 48;
            int gy = curY + 4;
            int gw = w - gx - 20;
            int gh = 66;

            // Plot Box Backdrop + gridlines
            p.setPen(QPen(QColor(25, 35, 50), 1));
            p.setBrush(QColor(6, 9, 14, 220));
            p.drawRect(gx, gy, gw, gh);

            p.setPen(QPen(QColor(30, 42, 60, 140), 1, Qt::DashLine));
            p.drawLine(gx, gy + gh * 0.25, gx + gw, gy + gh * 0.25);
            p.drawLine(gx, gy + gh * 0.50, gx + gw, gy + gh * 0.50);
            p.drawLine(gx, gy + gh * 0.75, gx + gw, gy + gh * 0.75);

            // Compute dynamic range
            float minVal = widget.rangeMin;
            float maxVal = widget.rangeMax;
            if (widget.autoScale) {
                for (const auto& line : widget.metrics) {
                    if (IsStringMetric(line.metricId)) continue;
                    const auto& hist = GetHistoryForMetric(line.metricId);
                    for (float v : hist) {
                        if (v < minVal) minVal = v;
                        if (v > maxVal) maxVal = v;
                    }
                }
                if (maxVal <= minVal) maxVal = minVal + 1.0f;
            }
            float range = maxVal - minVal;
            if (range <= 0.001f) range = 1.0f;

            // Y-axis labels
            p.setPen(QColor(130, 145, 170));
            p.setFont(QFont("sans-serif", 7, QFont::Normal));
            p.drawText(14, gy + 10, QString::number(maxVal, 'f', (maxVal < 10) ? 1 : 0));
            p.drawText(14, gy + gh, QString::number(minVal, 'f', (minVal < 10) ? 1 : 0));

            // Render each series
            for (const auto& line : widget.metrics) {
                if (IsStringMetric(line.metricId)) continue;
                const auto& hist = GetHistoryForMetric(line.metricId);
                if (hist.size() < 2) continue;

                float stepX = static_cast<float>(gw) / static_cast<float>(hist.size() - 1);
                auto getPlotY = [&](float v) -> float {
                    float norm = (v - minVal) / range;
                    norm = std::clamp(norm, 0.0f, 1.0f);
                    return (gy + gh) - (norm * gh);
                };

                // Translucent Area Fill
                if (line.fillColor.alpha() > 0) {
                    QPainterPath areaPath;
                    areaPath.moveTo(gx, gy + gh);
                    for (size_t i = 0; i < hist.size(); ++i) {
                        float px = gx + i * stepX;
                        float py = getPlotY(hist[i]);
                        areaPath.lineTo(px, py);
                    }
                    areaPath.lineTo(gx + (hist.size() - 1) * stepX, gy + gh);
                    areaPath.closeSubpath();
                    p.fillPath(areaPath, line.fillColor);
                }

                // Line curve
                p.setPen(QPen(line.lineColor, 1.8));
                bool isStepped = (line.statId == 6 || line.statId == 5); // 99% or 1%
                for (size_t i = 0; i < hist.size() - 1; ++i) {
                    float px0 = gx + i * stepX;
                    float py0 = getPlotY(hist[i]);
                    float px1 = gx + (i + 1) * stepX;
                    float py1 = getPlotY(hist[i + 1]);

                    if (isStepped) {
                        p.drawLine(QPointF(px0, py0), QPointF(px1, py0));
                        p.drawLine(QPointF(px1, py0), QPointF(px1, py1));
                    } else {
                        p.drawLine(QPointF(px0, py0), QPointF(px1, py1));
                    }
                }
            }

            // X-axis 10..0
            p.setPen(QColor(110, 125, 145));
            p.setFont(QFont("sans-serif", 7, QFont::Normal));
            p.drawText(gx, gy + gh + 12, "10");
            p.drawText(gx + gw - 8, gy + gh + 12, "0");

            curY = gy + gh + 18;
        }
    }
}

void WindowedOverlayWidget::mousePressEvent(QMouseEvent *event) {
    if (event->button() == Qt::LeftButton) {
        // Check if clicked close button 'X' (top right ~32px)
        if (event->pos().x() >= width() - 32 && event->pos().y() <= 32) {
            config_->overlayWindowedMode = false;
            config_->Save();
            hide();
            emit windowedClosed();
            return;
        }
        if (windowHandle()) {
            windowHandle()->startSystemMove();
        } else {
            dragging_ = true;
            dragPosition_ = event->globalPosition().toPoint() - frameGeometry().topLeft();
        }
        event->accept();
    }
}

void WindowedOverlayWidget::mouseMoveEvent(QMouseEvent *event) {
    if (dragging_ && (event->buttons() & Qt::LeftButton)) {
        move(event->globalPosition().toPoint() - dragPosition_);
        event->accept();
    }
}

void WindowedOverlayWidget::mouseReleaseEvent(QMouseEvent *) {
    dragging_ = false;
}

} // namespace gnumon::gui
