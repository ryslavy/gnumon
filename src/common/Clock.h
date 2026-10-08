#pragma once

#include <cstdint>
#include <chrono>

namespace gnumon::common {

class Clock {
public:
    // Returns current monotonic raw time in nanoseconds
    static uint64_t GetTimestampNs() noexcept;

    // Returns time in microseconds
    static uint64_t GetTimestampUs() noexcept {
        return GetTimestampNs() / 1000ULL;
    }

    // Returns time in milliseconds
    static double GetTimestampMs() noexcept {
        return static_cast<double>(GetTimestampNs()) / 1'000'000.0;
    }

    // QPC equivalent on Linux: monotonic raw nanoseconds
    static uint64_t GetQpc() noexcept {
        return GetTimestampNs();
    }

    // Frequency of QPC equivalent on Linux: 1 GHz (1,000,000,000 counts per sec)
    static constexpr uint64_t GetQpcFrequency() noexcept {
        return 1'000'000'000ULL;
    }
};

} // namespace gnumon::common
