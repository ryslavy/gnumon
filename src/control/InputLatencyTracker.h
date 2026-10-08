#pragma once

#include <cstdint>
#include <atomic>
#include <thread>
#include <string>
#include <vector>

namespace gnumon::control {

class InputLatencyTracker {
public:
    InputLatencyTracker();
    ~InputLatencyTracker();

    bool Start();
    void Stop();

    // Returns latest click timestamp in nanoseconds (CLOCK_MONOTONIC_RAW)
    uint64_t GetLastClickTimestampNs() const;

    // Global hotkey triggers
    bool ConsumeHotkeyToggle();
    bool ConsumeOverlayHotkeyToggle();
    bool ConsumeRecordHotkeyToggle();
    void SetHotkeyKeycode(uint16_t code);

private:
    void WorkerLoop();

    std::atomic<bool> running_{false};
    std::atomic<uint64_t> lastClickTimestampNs_{0};
    std::atomic<bool> overlayToggleTriggered_{false};
    std::atomic<bool> recordToggleTriggered_{false};
    uint16_t targetKeycode_ = 68; // KEY_F10 (recording), KEY_F11 (overlay)
    std::thread workerThread_;
    std::vector<int> inputFds_;
};

} // namespace gnumon::control
