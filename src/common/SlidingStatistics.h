#pragma once

#include <vector>
#include <deque>
#include <algorithm>
#include <numeric>
#include <cmath>
#include <cstdint>

namespace gnumon::common {

class SlidingStatistics {
public:
    struct Sample {
        uint64_t timestampNs = 0;
        double value = 0.0;
    };

    void Push(uint64_t timestampNs, double value) {
        samples_.push_back({timestampNs, value});
    }

    void PruneOlderThan(uint64_t cutoffTimestampNs) {
        while (!samples_.empty() && samples_.front().timestampNs < cutoffTimestampNs) {
            samples_.pop_front();
        }
    }

    size_t GetCount() const noexcept {
        return samples_.size();
    }

    bool IsEmpty() const noexcept {
        return samples_.empty();
    }

    double GetAverage() const {
        if (samples_.empty()) return 0.0;
        double sum = 0.0;
        for (const auto& s : samples_) {
            sum += s.value;
        }
        return sum / static_cast<double>(samples_.size());
    }

    double GetMin() const {
        if (samples_.empty()) return 0.0;
        double m = samples_.front().value;
        for (const auto& s : samples_) {
            if (s.value < m) m = s.value;
        }
        return m;
    }

    double GetMax() const {
        if (samples_.empty()) return 0.0;
        double m = samples_.front().value;
        for (const auto& s : samples_) {
            if (s.value > m) m = s.value;
        }
        return m;
    }

    // Percentile calculation (0.0 to 100.0)
    double GetPercentile(double percentile) const {
        if (samples_.empty()) return 0.0;
        if (samples_.size() == 1) return samples_.front().value;

        std::vector<double> vals;
        vals.reserve(samples_.size());
        for (const auto& s : samples_) {
            vals.push_back(s.value);
        }
        std::sort(vals.begin(), vals.end());

        double rank = (percentile / 100.0) * static_cast<double>(vals.size() - 1);
        size_t lower = static_cast<size_t>(std::floor(rank));
        size_t upper = static_cast<size_t>(std::ceil(rank));
        double weight = rank - static_cast<double>(lower);

        if (upper >= vals.size()) upper = vals.size() - 1;
        return vals[lower] * (1.0 - weight) + vals[upper] * weight;
    }

    // 1% Low is equivalent to 1st percentile of FPS or 99th percentile of FrameTime
    double Get1PercentLow() const {
        return GetPercentile(1.0);
    }

    // 0.1% Low is equivalent to 0.1th percentile of FPS
    double Get01PercentLow() const {
        return GetPercentile(0.1);
    }

    double Get95thPercentile() const {
        return GetPercentile(95.0);
    }

    double Get99thPercentile() const {
        return GetPercentile(99.0);
    }

    const std::deque<Sample>& GetRawSamples() const noexcept {
        return samples_;
    }

private:
    std::deque<Sample> samples_;
};

} // namespace gnumon::common
