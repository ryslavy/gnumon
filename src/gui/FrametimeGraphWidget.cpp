#include "FrametimeGraphWidget.h"
#include <QPainterPath>
#include <algorithm>
#include <vector>

namespace gnumon::gui {

FrametimeGraphWidget::FrametimeGraphWidget(QWidget *parent)
    : QWidget(parent)
{
    setMinimumHeight(120);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
}

void FrametimeGraphWidget::AddSample(double frametimeMs, double fps) {
    if (frametimeMs < 0.0) frametimeMs = 0.0;
    if (fps < 0.0) fps = 0.0;

    currentFrametimeMs_ = frametimeMs;
    currentFps_ = fps;

    frametimes_.push_back(frametimeMs);
    if (frametimes_.size() > MAX_SAMPLES) {
        frametimes_.pop_front();
    }

    fpsSamples_.push_back(fps);
    if (fpsSamples_.size() > MAX_SAMPLES) {
        fpsSamples_.pop_front();
    }

    // Calculate 99th percentile frametime (1% Low)
    if (!frametimes_.empty()) {
        std::vector<double> sorted(frametimes_.begin(), frametimes_.end());
        std::sort(sorted.begin(), sorted.end());
        size_t idx = static_cast<size_t>(sorted.size() * 0.99);
        if (idx >= sorted.size()) idx = sorted.size() - 1;
        p99FrametimeMs_ = sorted[idx];
    }

    update();
}

void FrametimeGraphWidget::Clear() {
    frametimes_.clear();
    fpsSamples_.clear();
    currentFps_ = 0.0;
    currentFrametimeMs_ = 0.0;
    p99FrametimeMs_ = 0.0;
    update();
}

void FrametimeGraphWidget::paintEvent(QPaintEvent* /*event*/) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    int w = width();
    int h = height();

    // Background
    painter.fillRect(0, 0, w, h, QColor(24, 25, 29));

    // Border
    painter.setPen(QColor(48, 52, 60));
    painter.drawRect(0, 0, w - 1, h - 1);

    // Reference grid lines:
    // 33.3ms (30 FPS), 16.6ms (60 FPS), 8.3ms (120 FPS)
    // Scale max frametime to at least 40ms or highest sample
    double maxMs = 40.0;
    for (double ft : frametimes_) {
        if (ft > maxMs) maxMs = ft;
    }
    // Cap at reasonable scale to avoid huge spikes flattening normal frames
    if (maxMs > 100.0) maxMs = 100.0;

    auto getY = [&](double ms) -> double {
        if (ms > maxMs) ms = maxMs;
        return h - 10.0 - (ms / maxMs) * (h - 20.0);
    };

    // Draw reference lines
    painter.setPen(QPen(QColor(60, 65, 75, 120), 1, Qt::DashLine));
    double lines[] = { 8.33, 16.66, 33.33 };
    const char* labels[] = { "120 FPS (8.3ms)", "60 FPS (16.6ms)", "30 FPS (33.3ms)" };

    for (int i = 0; i < 3; ++i) {
        double y = getY(lines[i]);
        if (y > 10.0 && y < h - 10.0) {
            painter.drawLine(0, static_cast<int>(y), w, static_cast<int>(y));
            painter.setPen(QColor(100, 110, 130));
            painter.drawText(w - 110, static_cast<int>(y) - 2, labels[i]);
            painter.setPen(QPen(QColor(60, 65, 75, 120), 1, Qt::DashLine));
        }
    }

    if (frametimes_.size() < 2) {
        painter.setPen(QColor(120, 130, 145));
        painter.drawText(rect(), Qt::AlignCenter, "Waiting for Vulkan frame stream...");
        return;
    }

    // Draw Frametime Curve
    QPainterPath path;
    double stepX = static_cast<double>(w) / static_cast<double>(MAX_SAMPLES);
    double startX = w - (frametimes_.size() * stepX);

    for (size_t i = 0; i < frametimes_.size(); ++i) {
        double x = startX + (i * stepX);
        double y = getY(frametimes_[i]);
        if (i == 0) {
            path.moveTo(x, y);
        } else {
            path.lineTo(x, y);
        }
    }

    // Glow / Line
    painter.setPen(QPen(QColor(76, 175, 80), 1.8)); // Green
    painter.drawPath(path);

    // Overlay Stats text in top-left
    painter.setPen(QColor(230, 235, 245));
    QFont font = painter.font();
    font.setBold(true);
    painter.setFont(font);

    double onePercentLowFps = (p99FrametimeMs_ > 0.0) ? (1000.0 / p99FrametimeMs_) : 0.0;
    QString statsText = QString("FPS: %1 | 1% Low: %2 FPS | Frametime: %3 ms")
        .arg(currentFps_, 0, 'f', 1)
        .arg(onePercentLowFps, 0, 'f', 1)
        .arg(currentFrametimeMs_, 0, 'f', 2);

    painter.drawText(12, 22, statsText);
}

} // namespace gnumon::gui
