#include "Clock.h"
#include <ctime>

namespace gnumon::common {

uint64_t Clock::GetTimestampNs() noexcept {
    struct timespec ts{};
    clock_gettime(CLOCK_MONOTONIC_RAW, &ts);
    return static_cast<uint64_t>(ts.tv_sec) * 1'000'000'000ULL + static_cast<uint64_t>(ts.tv_nsec);
}

} // namespace gnumon::common
