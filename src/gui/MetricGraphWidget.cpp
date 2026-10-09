#include "MetricGraphWidget.h"
#include <QPainterPath>
#include <QFontMetrics>
#include <QDateTime>
#include <cmath>
#include <numeric>

namespace gnumon::gui {

MetricGraphWidget::MetricGraphWidget(QWidget *parent)
    : QWidget(parent)
{
    setMinimumHeight(240);
    setMouseTracking(true);

    primarySeries_.name = "Metric 1";
    primarySeries_.unit = "";
    primarySeries_.color = QColor(0, 229, 255); // Cyan
    primarySeries_.enabled = true;

    secondarySeries_.name = "Metric 2";
    secondarySeries_.unit = "";
    secondarySeries_.color = QColor(255, 112, 67); // Coral Orange
    secondarySeries_.enabled = false;
}

void MetricGraphWidget::SetPrimarySeries(const QString& name, const QString& unit, const QColor& color) {
    primarySeries_.name = name;
    primarySeries_.unit = unit;
    primarySeries_.color = color;
    update();
}

void MetricGraphWidget::SetSecondarySeries(const QString& name, const QString& unit, const QColor& color) {
    secondarySeries_.name = name;
    secondarySeries_.unit = unit;
    secondarySeries_.color = color;
    update();
}

void MetricGraphWidget::EnableSecondarySeries(bool enable) {
    secondarySeries_.enabled = enable;
    update();
}

void MetricGraphWidget::SetTimeWindow(double seconds) {
    timeWindowSec_ = std::max(1.0, seconds);
    update();
}

void MetricGraphWidget::Clear() {
    primarySeries_.samples.clear();
    secondarySeries_.samples.clear();
    primarySeries_.minValue = primarySeries_.maxValue = primarySeries_.avgValue = primarySeries_.p99Value = primarySeries_.currentValue = 0.0;
    secondarySeries_.minValue = secondarySeries_.maxValue = secondarySeries_.avgValue = secondarySeries_.p99Value = secondarySeries_.currentValue = 0.0;
    update();
}

void MetricGraphWidget::AddSamplePrimary(double value, double timestampSec) {
    if (timestampSec <= 0.0) {
        timestampSec = static_cast<double>(QDateTime::currentMSecsSinceEpoch()) / 1000.0;
    }
    primarySeries_.samples.push_back({ timestampSec, value });
    primarySeries_.currentValue = value;

    // Prune samples older than timeWindowSec_
    double cutoff = timestampSec - timeWindowSec_;
    while (!primarySeries_.samples.empty() && primarySeries_.samples.front().timestampSec < cutoff) {
        primarySeries_.samples.pop_front();
    }
    UpdateStats(primarySeries_);
    update();
}

void MetricGraphWidget::AddSampleSecondary(double value, double timestampSec) {
    if (!secondarySeries_.enabled) return;
    if (timestampSec <= 0.0) {
        timestampSec = static_cast<double>(QDateTime::currentMSecsSinceEpoch()) / 1000.0;
    }
    secondarySeries_.samples.push_back({ timestampSec, value });
    secondarySeries_.currentValue = value;

    double cutoff = timestampSec - timeWindowSec_;
    while (!secondarySeries_.samples.empty() && secondarySeries_.samples.front().timestampSec < cutoff) {
        secondarySeries_.samples.pop_front();
    }
    UpdateStats(secondarySeries_);
    update();
}

void MetricGraphWidget::UpdateStats(GraphSeries& series) {
    if (series.samples.empty()) {
        series.minValue = series.maxValue = series.avgValue = series.p99Value = 0.0;
        return;
    }

    double minV = series.samples.front().value;
    double maxV = series.samples.front().value;
    double sum = 0.0;
    std::vector<double> vals;
    vals.reserve(series.samples.size());

    for (const auto& s : series.samples) {
        if (s.value < minV) minV = s.value;
        if (s.value > maxV) maxV = s.value;
        sum += s.value;
        vals.push_back(s.value);
    }

    series.minValue = minV;
    series.maxValue = maxV;
    series.avgValue = sum / static_cast<double>(series.samples.size());

    std::sort(vals.begin(), vals.end());
    size_t idx99 = static_cast<size_t>(std::floor(0.99 * static_cast<double>(vals.size() - 1)));
    series.p99Value = vals[idx99];
}

void MetricGraphWidget::mouseMoveEvent(QMouseEvent *event) {
    hoverActive_ = true;
    hoverPos_ = event->pos();
    update();
}

void MetricGraphWidget::leaveEvent(QEvent *event) {
    Q_UNUSED(event);
    hoverActive_ = false;
    update();
}

void MetricGraphWidget::paintEvent(QPaintEvent *event) {
    Q_UNUSED(event);
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    int w = width();
    int h = height();

    // Background gradient
    QLinearGradient bgGrad(0, 0, 0, h);
    bgGrad.setColorAt(0.0, QColor(20, 24, 32));
    bgGrad.setColorAt(1.0, QColor(14, 17, 23));
    p.fillRect(rect(), bgGrad);

    // Graph Area boundaries with padding for axes
    int padLeft = 60;
    int padRight = secondarySeries_.enabled ? 60 : 20;
    int padTop = 32;
    int padBottom = 30;

    QRect plotRect(padLeft, padTop, w - padLeft - padRight, h - padTop - padBottom);
    if (plotRect.width() <= 10 || plotRect.height() <= 10) return;

    // Border of plot area
    p.setPen(QPen(QColor(42, 49, 61), 1));
    p.drawRect(plotRect);

    // Horizontal Grid Lines & Labels
    const int numYGrid = 4;
    p.setFont(QFont("Segoe UI", 9));

    // Determine Y range for primary series
    double primMin = std::min(0.0, primarySeries_.minValue);
    double primMax = std::max(1.0, primarySeries_.maxValue * 1.15);
    if (primMax - primMin < 0.001) primMax = primMin + 1.0;

    for (int i = 0; i <= numYGrid; ++i) {
        float frac = static_cast<float>(i) / static_cast<float>(numYGrid);
        int y = plotRect.bottom() - static_cast<int>(frac * plotRect.height());

        p.setPen(QPen(QColor(32, 38, 48), 1, Qt::DashLine));
        p.drawLine(plotRect.left(), y, plotRect.right(), y);

        double val = primMin + frac * (primMax - primMin);
        p.setPen(QColor(120, 144, 156));
        QString primLabel = QString::number(val, 'f', (primMax > 10.0) ? 1 : 2);
        p.drawText(QRect(4, y - 8, padLeft - 10, 16), Qt::AlignRight | Qt::AlignVCenter, primLabel);

        // If secondary series is enabled, draw right axis label
        if (secondarySeries_.enabled) {
            double secMin = std::min(0.0, secondarySeries_.minValue);
            double secMax = std::max(1.0, secondarySeries_.maxValue * 1.15);
            if (secMax - secMin < 0.001) secMax = secMin + 1.0;
            double sVal = secMin + frac * (secMax - secMin);
            p.setPen(secondarySeries_.color);
            QString secLabel = QString::number(sVal, 'f', (secMax > 10.0) ? 1 : 2);
            p.drawText(QRect(plotRect.right() + 6, y - 8, padRight - 10, 16), Qt::AlignLeft | Qt::AlignVCenter, secLabel);
        }
    }

    // Time axis labels (0s to -timeWindowSec_)
    p.setPen(QColor(90, 107, 124));
    p.drawText(plotRect.right() - 25, plotRect.bottom() + 18, "Now");
    p.drawText(plotRect.left(), plotRect.bottom() + 18, QString("-%1s").arg(timeWindowSec_, 0, 'f', 0));
    p.drawText(plotRect.center().x() - 15, plotRect.bottom() + 18, QString("-%1s").arg(timeWindowSec_ / 2.0, 0, 'f', 0));

    // Draw Series Helper
    auto drawSeries = [&](const GraphSeries& s, double yMin, double yMax) {
        if (s.samples.size() < 2) return;

        double latestTime = s.samples.back().timestampSec;
        double startTime = latestTime - timeWindowSec_;

        QPainterPath path;
        QPainterPath fillPath;
        bool started = false;
        QPointF firstPt, lastPt;

        for (const auto& sample : s.samples) {
            if (sample.timestampSec < startTime) continue;

            double timeFrac = (sample.timestampSec - startTime) / timeWindowSec_;
            double valFrac = (sample.value - yMin) / (yMax - yMin);
            valFrac = std::clamp(valFrac, 0.0, 1.0);

            float x = plotRect.left() + static_cast<float>(timeFrac * plotRect.width());
            float y = plotRect.bottom() - static_cast<float>(valFrac * plotRect.height());

            if (!started) {
                path.moveTo(x, y);
                fillPath.moveTo(x, plotRect.bottom());
                fillPath.lineTo(x, y);
                firstPt = QPointF(x, y);
                started = true;
            } else {
                path.lineTo(x, y);
                fillPath.lineTo(x, y);
            }
            lastPt = QPointF(x, y);
        }

        if (started) {
            fillPath.lineTo(lastPt.x(), plotRect.bottom());
            fillPath.closeSubpath();

            // Gradient Fill under curve
            QLinearGradient fillGrad(0, plotRect.top(), 0, plotRect.bottom());
            QColor c1 = s.color;
            c1.setAlpha(40);
            QColor c2 = s.color;
            c2.setAlpha(5);
            fillGrad.setColorAt(0.0, c1);
            fillGrad.setColorAt(1.0, c2);

            p.setPen(Qt::NoPen);
            p.fillPath(fillPath, fillGrad);

            // Curve stroke
            p.setPen(QPen(s.color, 2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            p.drawPath(path);

            // Latest sample highlight point
            p.setBrush(s.color);
            p.drawEllipse(lastPt, 3.5, 3.5);
        }
    };

    // Draw Primary Series
    drawSeries(primarySeries_, primMin, primMax);

    // Draw Secondary Series
    if (secondarySeries_.enabled) {
        double secMin = std::min(0.0, secondarySeries_.minValue);
        double secMax = std::max(1.0, secondarySeries_.maxValue * 1.15);
        if (secMax - secMin < 0.001) secMax = secMin + 1.0;
        drawSeries(secondarySeries_, secMin, secMax);
    }

    // Top Header Legend
    p.setFont(QFont("Segoe UI", 10, QFont::Bold));
    p.setPen(primarySeries_.color);
    QString primLegend = QString("%1: %2 %3 (Avg: %4, Max: %5)")
        .arg(primarySeries_.name)
        .arg(primarySeries_.currentValue, 0, 'f', 2)
        .arg(primarySeries_.unit)
        .arg(primarySeries_.avgValue, 0, 'f', 2)
        .arg(primarySeries_.maxValue, 0, 'f', 2);
    p.drawText(plotRect.left(), padTop - 10, primLegend);

    if (secondarySeries_.enabled) {
        p.setPen(secondarySeries_.color);
        QString secLegend = QString(" | %1: %2 %3")
            .arg(secondarySeries_.name)
            .arg(secondarySeries_.currentValue, 0, 'f', 2)
            .arg(secondarySeries_.unit);
        QFontMetrics fm(p.font());
        int primW = fm.horizontalAdvance(primLegend);
        p.drawText(plotRect.left() + primW + 10, padTop - 10, secLegend);
    }

    // Hover Crosshair
    if (hoverActive_ && plotRect.contains(hoverPos_)) {
        p.setPen(QPen(QColor(255, 255, 255, 90), 1, Qt::DashLine));
        p.drawLine(hoverPos_.x(), plotRect.top(), hoverPos_.x(), plotRect.bottom());
        p.drawLine(plotRect.left(), hoverPos_.y(), plotRect.right(), hoverPos_.y());
    }
}

} // namespace gnumon::gui
