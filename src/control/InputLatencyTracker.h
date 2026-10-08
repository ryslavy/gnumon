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
    bool ConsumeInGameHudHotkeyToggle();
    bool ConsumeOverlayHotkeyToggle();
    bool ConsumeRecordHotkeyToggle();
    bool ConsumeMiniHudHotkeyToggle();

    void SetHotkeys(const std::string& inGameHud, const std::string& record,
                    const std::string& overlay, const std::string& miniHud);
    void SetHotkeyKeycode(uint16_t code);

private:
    void WorkerLoop();

    std::atomic<bool> running_{false};
    std::atomic<uint64_t> lastClickTimestampNs_{0};
    std::atomic<bool> inGameHudToggleTriggered_{false};
    std::atomic<bool> overlayToggleTriggered_{false};
    std::atomic<bool> recordToggleTriggered_{false};
    std::atomic<bool> miniHudToggleTriggered_{false};

    uint16_t evdevInGameHud_ = 67; // KEY_F9
    uint16_t evdevRecord_ = 68;    // KEY_F10
    uint16_t evdevOverlay_ = 87;   // KEY_F11
    uint16_t evdevMiniHud_ = 88;   // KEY_F12

    std::string keyNameInGameHud_ = "F9";
    std::string keyNameRecord_ = "F10";
    std::string keyNameOverlay_ = "F11";
    std::string keyNameMiniHud_ = "F12";

    std::thread workerThread_;
    std::vector<int> inputFds_;
};

} // namespace gnumon::control
