#include "InputLatencyTracker.h"
#include "../common/Clock.h"
#include <linux/input.h>
#include <fcntl.h>
#include <unistd.h>
#include <poll.h>
#include <filesystem>
#include <iostream>

namespace gnumon::control {

InputLatencyTracker::InputLatencyTracker() = default;

InputLatencyTracker::~InputLatencyTracker() {
    Stop();
}

bool InputLatencyTracker::Start() {
    if (running_.load()) return true;

    inputFds_.clear();

    // Scan /dev/input/event*
    std::filesystem::path inputDir("/dev/input");
    if (!std::filesystem::exists(inputDir)) return false;

    for (const auto& entry : std::filesystem::directory_iterator(inputDir)) {
        std::string filename = entry.path().filename().string();
        if (filename.rfind("event", 0) == 0) {
            int fd = open(entry.path().c_str(), O_RDONLY | O_NONBLOCK);
            if (fd >= 0) {
                // Check if device has EV_KEY capabilities
                unsigned long evBits[(EV_MAX + 7) / 8]{};
                if (ioctl(fd, EVIOCGBIT(0, sizeof(evBits)), evBits) >= 0) {
                    if (evBits[0] & (1 << EV_KEY)) {
                        inputFds_.push_back(fd);
                    } else {
                        close(fd);
                    }
                } else {
                    close(fd);
                }
            }
        }
    }

    if (inputFds_.empty()) {
        return false;
    }

    running_.store(true);
    workerThread_ = std::thread(&InputLatencyTracker::WorkerLoop, this);
    return true;
}

void InputLatencyTracker::Stop() {
    if (running_.exchange(false)) {
        if (workerThread_.joinable()) {
            workerThread_.join();
        }
    }

    for (int fd : inputFds_) {
        close(fd);
    }
    inputFds_.clear();
}

uint64_t InputLatencyTracker::GetLastClickTimestampNs() const {
    return lastClickTimestampNs_.load(std::memory_order_acquire);
}

bool InputLatencyTracker::ConsumeHotkeyToggle() {
    return ConsumeRecordHotkeyToggle();
}

bool InputLatencyTracker::ConsumeOverlayHotkeyToggle() {
    return overlayToggleTriggered_.exchange(false, std::memory_order_acq_rel);
}

bool InputLatencyTracker::ConsumeRecordHotkeyToggle() {
    return recordToggleTriggered_.exchange(false, std::memory_order_acq_rel);
}

void InputLatencyTracker::SetHotkeyKeycode(uint16_t code) {
    targetKeycode_ = code;
}

void InputLatencyTracker::WorkerLoop() {
    std::vector<pollfd> pFds;
    for (int fd : inputFds_) {
        pFds.push_back({ fd, POLLIN, 0 });
    }

    while (running_.load(std::memory_order_relaxed)) {
        int ret = poll(pFds.data(), pFds.size(), 50);
        if (ret <= 0) continue;

        for (auto& pfd : pFds) {
            if (pfd.revents & POLLIN) {
                struct input_event ev[16];
                ssize_t n = read(pfd.fd, ev, sizeof(ev));
                if (n >= static_cast<ssize_t>(sizeof(struct input_event))) {
                    size_t count = n / sizeof(struct input_event);
                    for (size_t i = 0; i < count; ++i) {
                        if (ev[i].type == EV_KEY && ev[i].value == 1) { // Key/Button pressed down
                            // BTN_LEFT (0x110), BTN_RIGHT (0x111), BTN_MIDDLE (0x112)
                            if (ev[i].code == BTN_LEFT || ev[i].code == BTN_RIGHT || ev[i].code == BTN_MIDDLE) {
                                uint64_t ts = common::Clock::GetTimestampNs();
                                lastClickTimestampNs_.store(ts, std::memory_order_release);
                            }
                            if (ev[i].code == 87) { // KEY_F11: Toggle Overlay
                                overlayToggleTriggered_.store(true, std::memory_order_release);
                            }
                            if (ev[i].code == 68 || ev[i].code == targetKeycode_) { // KEY_F10: Toggle Capture/Record
                                recordToggleTriggered_.store(true, std::memory_order_release);
                            }
                        }
                    }
                }
            }
        }
    }
}

} // namespace gnumon::control
