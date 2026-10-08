#include "PresentMonOverlay.h"
#include <QGuiApplication>
#include <QScreen>
#include <QFontDatabase>
#include <algorithm>
#include <iomanip>
#include <sstream>
#include <fstream>
#include "../common/ProcUtils.h"

namespace gnumon::gui {

namespace {
struct OverlayQueryPayload {
    double gpuPower = 0.0;
    double gpuTemp = 0.0;
    double gpuUtil = 0.0;
    double gpuFreq = 0.0;

    double cpuUtil = 0.0;
    double cpuPower = 0.0;
    double cpuTemp = 0.0;
    double cpuFreq = 0.0;

    double displayedFps = 0.0;
    double frameTimeMs = 0.0;
    double gpuTimeMs = 0.0;
    double gpuWaitMs = 0.0;
};
}

PresentMonOverlay::PresentMonOverlay(PM_SESSION_HANDLE session, QWidget *parent)
    : QWidget(parent, Qt::Window | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::SubWindow)
    , session_(session)
{
    setAttribute(Qt::WA_TranslucentBackground, true);
    setAttribute(Qt::WA_ShowWithoutActivating, true);
    setFocusPolicy(Qt::NoFocus);
    resize(360, 315);

    UpdateDynamicQuery();
    Reposition();

    pollTimer_ = new QTimer(this);
    connect(pollTimer_, &QTimer::timeout, this, &PresentMonOverlay::OnPollTimer);
    pollTimer_->start(33); // ~30 Hz smooth refresh
}

PresentMonOverlay::~PresentMonOverlay() {
    if (query_) {
        pmFreeDynamicQuery(query_);
    }
}

void PresentMonOverlay::SetTargetProcess(uint32_t pid, const std::string& name) {
    trackedPid_ = pid;
    processName_ = name.empty() ? ("PID " + std::to_string(pid)) : name;
    history_.clear();
}

void PresentMonOverlay::SetRecordingState(bool isRecording, const std::string& /*csvPath*/) {
    if (isRecording && !isRecording_) {
        recordingStartTime_ = std::chrono::steady_clock::now();
        recordedFramesCount_ = 0;
    }
    isRecording_ = isRecording;
    update();
}

void PresentMonOverlay::ToggleVisibility() {
    if (isVisible()) {
        hide();
    } else {
        show();
        raise();
    }
}

void PresentMonOverlay::SetCorner(int corner) {
    corner_ = corner % 4;
    Reposition();
}

void PresentMonOverlay::Reposition() {
    QScreen *screen = QGuiApplication::primaryScreen();
    if (!screen) return;

    QRect geom = screen->availableGeometry();
    constexpr int margin = 24;
    int x = margin;
    int y = margin;

    switch (corner_) {
    case 0: // Top-Left
        x = geom.left() + margin;
        y = geom.top() + margin;
        break;
    case 1: // Top-Right
        x = geom.right() - width() - margin;
        y = geom.top() + margin;
        break;
    case 2: // Bottom-Left
        x = geom.left() + margin;
        y = geom.bottom() - height() - margin;
        break;
    case 3: // Bottom-Right
        x = geom.right() - width() - margin;
        y = geom.bottom() - height() - margin;
        break;
    }

    move(x, y);
}

void PresentMonOverlay::UpdateDynamicQuery() {
    if (!session_) return;
    if (query_) {
        pmFreeDynamicQuery(query_);
        query_ = nullptr;
    }

    std::vector<PM_QUERY_ELEMENT> elements = {
        { PM_METRIC_GPU_POWER, PM_STAT_NONE, 0, 0, offsetof(OverlayQueryPayload, gpuPower), sizeof(double) },
        { PM_METRIC_GPU_TEMPERATURE, PM_STAT_NONE, 0, 0, offsetof(OverlayQueryPayload, gpuTemp), sizeof(double) },
        { PM_METRIC_GPU_UTILIZATION, PM_STAT_NONE, 0, 0, offsetof(OverlayQueryPayload, gpuUtil), sizeof(double) },
        { PM_METRIC_GPU_FREQUENCY, PM_STAT_NONE, 0, 0, offsetof(OverlayQueryPayload, gpuFreq), sizeof(double) },

        { PM_METRIC_CPU_UTILIZATION, PM_STAT_NONE, 0, 0, offsetof(OverlayQueryPayload, cpuUtil), sizeof(double) },
        { PM_METRIC_CPU_POWER, PM_STAT_NONE, 0, 0, offsetof(OverlayQueryPayload, cpuPower), sizeof(double) },
        { PM_METRIC_CPU_TEMPERATURE, PM_STAT_NONE, 0, 0, offsetof(OverlayQueryPayload, cpuTemp), sizeof(double) },
        { PM_METRIC_CPU_FREQUENCY, PM_STAT_NONE, 0, 0, offsetof(OverlayQueryPayload, cpuFreq), sizeof(double) },

        { PM_METRIC_DISPLAYED_FPS, PM_STAT_AVG, 0, 0, offsetof(OverlayQueryPayload, displayedFps), sizeof(double) },
        { PM_METRIC_CPU_FRAME_TIME, PM_STAT_NONE, 0, 0, offsetof(OverlayQueryPayload, frameTimeMs), sizeof(double) },
        { PM_METRIC_GPU_TIME, PM_STAT_NONE, 0, 0, offsetof(OverlayQueryPayload, gpuTimeMs), sizeof(double) },
        { PM_METRIC_GPU_WAIT, PM_STAT_NONE, 0, 0, offsetof(OverlayQueryPayload, gpuWaitMs), sizeof(double) },
    };

    pmRegisterDynamicQuery(session_, &query_, elements.data(), elements.size(), 1000.0, 0.0);
}

void PresentMonOverlay::OnPollTimer() {
    if (!query_) return;

    // Autotarget live game if not tracking specific PID
    if (trackedPid_ == 0) {
        auto activePids = common::GetActiveRingPids();
        if (!activePids.empty()) {
            uint32_t activePid = activePids.front();
            std::string comm = "Game";
            std::ifstream commFile("/proc/" + std::to_string(activePid) + "/comm");
            if (commFile.is_open()) std::getline(commFile, comm);
            SetTargetProcess(activePid, comm);
            pmStartTrackingProcess(session_, activePid);
        }
    }

    OverlayQueryPayload data{};
    uint32_t numSwapChains = 0;
    if (pmPollDynamicQuery(query_, trackedPid_, reinterpret_cast<uint8_t*>(&data), &numSwapChains) == PM_STATUS_SUCCESS) {
        displayedFps_ = data.displayedFps;
        frameTimeMs_ = data.frameTimeMs;
        gpuTimeMs_ = data.gpuTimeMs;
        gpuWaitMs_ = data.gpuWaitMs;

        gpuPower_ = data.gpuPower;
        gpuTemp_ = data.gpuTemp;
        gpuUtil_ = data.gpuUtil;
        gpuFreq_ = data.gpuFreq;

        cpuUtil_ = data.cpuUtil;
        cpuPower_ = data.cpuPower;
        cpuTemp_ = data.cpuTemp;
        cpuFreq_ = data.cpuFreq;

        if (frameTimeMs_ > 0.0 || gpuTimeMs_ > 0.0) {
            history_.push_back({ frameTimeMs_, gpuTimeMs_ });
            if (history_.size() > MAX_SAMPLES) {
                history_.pop_front();
            }
        }
    }

    if (isRecording_) {
        recordedFramesCount_++;
    }

    update();
}

void PresentMonOverlay::paintEvent(QPaintEvent * /*event*/) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    const int w = width();
    const int h = height();

    // 1. Sleek Intel PresentMon Translucent Window Frame
    QPainterPath bgPath;
    bgPath.addRoundedRect(0, 0, w, h, 10, 10);
    p.fillPath(bgPath, QColor(14, 18, 24, 225));

    QPen borderPen(QColor(0, 188, 212, 180), 1.5);
    p.setPen(borderPen);
    p.drawPath(bgPath);

    // 2. Header Bar
    QFont brandFont("SansSerif", 11, QFont::Bold);
    p.setFont(brandFont);
    p.setPen(QColor(0, 229, 255));
    p.drawText(16, 26, "PresentMon");

    QFont targetFont("SansSerif", 9);
    p.setFont(targetFont);
    p.setPen(QColor(160, 175, 195));
    QString procStr = QString("[%1]").arg(QString::fromStdString(processName_));
    p.drawText(115, 26, procStr);

    // REC Badge (blinking red circle + timer)
    if (isRecording_) {
        auto now = std::chrono::steady_clock::now();
        auto elapsedSec = std::chrono::duration_cast<std::chrono::seconds>(now - recordingStartTime_).count();
        int mins = static_cast<int>(elapsedSec / 60);
        int secs = static_cast<int>(elapsedSec % 60);

        bool blink = (elapsedSec % 2 == 0) || ((std::chrono::duration_cast<std::chrono::milliseconds>(now - recordingStartTime_).count() % 1000) < 500);
        if (blink) {
            p.setBrush(QColor(244, 67, 54));
            p.setPen(Qt::NoPen);
            p.drawEllipse(w - 95, 17, 10, 10);
        }

        QFont recFont("SansSerif", 9, QFont::Bold);
        p.setFont(recFont);
        p.setPen(QColor(244, 67, 54));
        QString recText = QString("REC %1:%2")
            .arg(mins, 2, 10, QChar('0'))
            .arg(secs, 2, 10, QChar('0'));
        p.drawText(w - 80, 26, recText);
    }

    // Divider
    p.setPen(QColor(255, 255, 255, 30));
    p.drawLine(14, 34, w - 14, 34);

    // 3. Primary Readout: FPS (Large Cyan) & Frametimes
    QFont fpsNumFont("SansSerif", 24, QFont::Bold);
    p.setFont(fpsNumFont);
    p.setPen(QColor(0, 229, 255));
    double effectiveFps = (displayedFps_ > 0.0) ? displayedFps_ : ((frameTimeMs_ > 0.0) ? (1000.0 / frameTimeMs_) : 0.0);
    p.drawText(16, 72, QString::number(effectiveFps, 'f', 1));

    QFont fpsUnitFont("SansSerif", 10, QFont::Bold);
    p.setFont(fpsUnitFont);
    p.setPen(QColor(0, 188, 212));
    p.drawText(115, 70, "FPS");

    // Right Column: CPU FT, GPU Busy, GPU Wait
    QFont metricLabelFont("SansSerif", 8, QFont::DemiBold);
    QFont metricValFont("Monospace", 9, QFont::Bold);

    // CPU Frame Time
    p.setFont(metricLabelFont);
    p.setPen(QColor(160, 175, 195));
    p.drawText(180, 52, "CPU Frame Time");
    p.setFont(metricValFont);
    p.setPen(QColor(129, 199, 132)); // Green
    p.drawText(290, 52, QString("%1 ms").arg(frameTimeMs_, 5, 'f', 1));

    // GPU Busy
    p.setFont(metricLabelFont);
    p.setPen(QColor(160, 175, 195));
    p.drawText(180, 68, "GPU Busy");
    p.setFont(metricValFont);
    p.setPen(QColor(79, 195, 247)); // Cyan
    p.drawText(290, 68, QString("%1 ms").arg(gpuTimeMs_, 5, 'f', 1));

    // GPU Wait
    p.setFont(metricLabelFont);
    p.setPen(QColor(160, 175, 195));
    p.drawText(180, 84, "GPU Wait");
    p.setFont(metricValFont);
    p.setPen(QColor(255, 183, 77)); // Amber
    p.drawText(290, 84, QString("%1 ms").arg(gpuWaitMs_, 5, 'f', 1));

    // Divider
    p.setPen(QColor(255, 255, 255, 25));
    p.drawLine(14, 94, w - 14, 94);

    // 4. Secondary Telemetry Grid (GPU & CPU)
    // GPU Row
    p.setFont(QFont("SansSerif", 8, QFont::Bold));
    p.setPen(QColor(0, 229, 255));
    p.drawText(16, 110, "GPU");

    p.setFont(QFont("Monospace", 8, QFont::DemiBold));
    p.setPen(QColor(220, 230, 240));
    QString gpuStats = QString("%1°C   %2W   %3%   %4MHz")
        .arg(gpuTemp_, 0, 'f', 0)
        .arg(gpuPower_, 0, 'f', 0)
        .arg(gpuUtil_, 0, 'f', 0)
        .arg(gpuFreq_, 0, 'f', 0);
    p.drawText(56, 110, gpuStats);

    // CPU Row
    p.setFont(QFont("SansSerif", 8, QFont::Bold));
    p.setPen(QColor(129, 199, 132));
    p.drawText(16, 126, "CPU");

    p.setFont(QFont("Monospace", 8, QFont::DemiBold));
    p.setPen(QColor(220, 230, 240));
    QString cpuStats = QString("%1°C   %2W   %3%   %4MHz")
        .arg(cpuTemp_, 0, 'f', 0)
        .arg(cpuPower_, 0, 'f', 0)
        .arg(cpuUtil_, 0, 'f', 0)
        .arg(cpuFreq_, 0, 'f', 0);
    p.drawText(56, 126, cpuStats);

    // 5. Embedded Graph (PresentMon GraphElement)
    const int gx = 14;
    const int gy = 138;
    const int gw = w - 28;
    const int gh = 145;

    p.setPen(QPen(QColor(255, 255, 255, 30), 1));
    p.setBrush(QColor(8, 12, 16, 210));
    p.drawRoundedRect(gx, gy, gw, gh, 6, 6);

    // Reference horizontal gridlines (16.6ms for 60fps, 33.3ms for 30fps)
    constexpr double maxFt = 40.0;
    auto msToY = [&](double ms) -> int {
        double clamped = std::clamp(ms, 0.0, maxFt);
        return gy + gh - static_cast<int>((clamped / maxFt) * gh);
    };

    // 16.6 ms line (60 FPS)
    int y60 = msToY(16.66);
    p.setPen(QPen(QColor(76, 175, 80, 80), 1, Qt::DashLine));
    p.drawLine(gx + 2, y60, gx + gw - 2, y60);
    p.setFont(QFont("Monospace", 7));
    p.setPen(QColor(76, 175, 80, 160));
    p.drawText(gx + 4, y60 - 2, "16.6ms (60 FPS)");

    // 33.3 ms line (30 FPS)
    int y30 = msToY(33.33);
    p.setPen(QPen(QColor(244, 67, 54, 70), 1, Qt::DashLine));
    p.drawLine(gx + 2, y30, gx + gw - 2, y30);
    p.setPen(QColor(244, 67, 54, 150));
    p.drawText(gx + 4, y30 - 2, "33.3ms (30 FPS)");

    // Plot History Curves
    if (history_.size() >= 2) {
        // Curve 1: Frametime (Cyan)
        QPolygon ftPoly;
        for (size_t i = 0; i < history_.size(); ++i) {
            int px = gx + static_cast<int>((static_cast<double>(i) / (MAX_SAMPLES - 1)) * gw);
            int py = msToY(history_[i].frameTimeMs);
            ftPoly << QPoint(px, py);
        }
        p.setPen(QPen(QColor(0, 229, 255), 1.8));
        p.drawPolyline(ftPoly);

        // Curve 2: GPU Busy (Amber/Orange)
        QPolygon gpuPoly;
        for (size_t i = 0; i < history_.size(); ++i) {
            int px = gx + static_cast<int>((static_cast<double>(i) / (MAX_SAMPLES - 1)) * gw);
            int py = msToY(history_[i].gpuTimeMs);
            gpuPoly << QPoint(px, py);
        }
        p.setPen(QPen(QColor(255, 183, 77), 1.5));
        p.drawPolyline(gpuPoly);
    }

    // Legend & Controls footer
    p.setFont(QFont("SansSerif", 7, QFont::DemiBold));
    // CPU FT dot
    p.setBrush(QColor(0, 229, 255));
    p.setPen(Qt::NoPen);
    p.drawEllipse(18, h - 16, 6, 6);
    p.setPen(QColor(160, 175, 195));
    p.drawText(28, h - 10, "CPU FT");

    // GPU Busy dot
    p.setBrush(QColor(255, 183, 77));
    p.drawEllipse(75, h - 16, 6, 6);
    p.setPen(QColor(160, 175, 195));
    p.drawText(85, h - 10, "GPU Busy");

    // Hotkey Hint
    p.setFont(QFont("Monospace", 7));
    p.setPen(QColor(120, 135, 150));
    p.drawText(w - 155, h - 10, "F11: Hide | F10: Rec");
}

void PresentMonOverlay::mousePressEvent(QMouseEvent *event) {
    if (event->button() == Qt::LeftButton) {
        dragPosition_ = event->globalPosition().toPoint() - frameGeometry().topLeft();
        event->accept();
    } else if (event->button() == Qt::RightButton) {
        // Right-click cycles corners
        SetCorner(corner_ + 1);
        event->accept();
    }
}

void PresentMonOverlay::mouseMoveEvent(QMouseEvent *event) {
    if (event->buttons() & Qt::LeftButton) {
        move(event->globalPosition().toPoint() - dragPosition_);
        event->accept();
    }
}

void PresentMonOverlay::keyPressEvent(QKeyEvent *event) {
    if (event->key() == Qt::Key_F11) {
        ToggleVisibility();
        event->accept();
        return;
    }
    QWidget::keyPressEvent(event);
}

} // namespace gnumon::gui
