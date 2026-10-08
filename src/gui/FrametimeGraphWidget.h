#pragma once

#include <QWidget>
#include <QPainter>
#include <deque>

namespace gnumon::gui {

class FrametimeGraphWidget : public QWidget {
    Q_OBJECT

public:
    explicit FrametimeGraphWidget(QWidget *parent = nullptr);
    ~FrametimeGraphWidget() override = default;

    void AddSample(double frametimeMs, double fps);
    void Clear();

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    static constexpr size_t MAX_SAMPLES = 180;
    std::deque<double> frametimes_;
    std::deque<double> fpsSamples_;
    double currentFps_ = 0.0;
    double currentFrametimeMs_ = 0.0;
    double p99FrametimeMs_ = 0.0; // 1% Low frametime
};

} // namespace gnumon::gui
