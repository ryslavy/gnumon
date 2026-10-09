#pragma once

#include <QWidget>
#include <QPainter>
#include <QMouseEvent>
#include <QString>
#include <deque>
#include <vector>
#include <algorithm>

namespace gnumon::gui {

struct MetricSample {
    double timestampSec;
    double value;
};

struct GraphSeries {
    QString name;
    QString unit;
    QColor color;
    std::deque<MetricSample> samples;
    bool enabled = true;
    double minValue = 0.0;
    double maxValue = 1.0;
    double avgValue = 0.0;
    double p99Value = 0.0;
    double currentValue = 0.0;
};

class MetricGraphWidget : public QWidget {
    Q_OBJECT

public:
    explicit MetricGraphWidget(QWidget *parent = nullptr);
    ~MetricGraphWidget() override = default;

    void SetPrimarySeries(const QString& name, const QString& unit, const QColor& color = QColor(0, 229, 255));
    void SetSecondarySeries(const QString& name, const QString& unit, const QColor& color = QColor(255, 112, 67));
    void EnableSecondarySeries(bool enable);

    void AddSamplePrimary(double value, double timestampSec = 0.0);
    void AddSampleSecondary(double value, double timestampSec = 0.0);

    void SetTimeWindow(double seconds);
    double GetTimeWindow() const { return timeWindowSec_; }

    void Clear();

    const GraphSeries& GetPrimarySeries() const { return primarySeries_; }
    const GraphSeries& GetSecondarySeries() const { return secondarySeries_; }

signals:
    void PointHovered(double time, double val1, double val2);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void leaveEvent(QEvent *event) override;

private:
    void UpdateStats(GraphSeries& series);

    GraphSeries primarySeries_;
    GraphSeries secondarySeries_;
    double timeWindowSec_ = 10.0;

    bool hoverActive_ = false;
    QPoint hoverPos_;
    double hoverVal1_ = 0.0;
    double hoverVal2_ = 0.0;
};

} // namespace gnumon::gui
