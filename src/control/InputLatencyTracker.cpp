#include "InputLatencyTracker.h"
#include "../common/Clock.h"
#include <linux/input.h>
#include <fcntl.h>
#include <unistd.h>
#include <poll.h>
#include <filesystem>
#include <chrono>
#include <iostream>
#if !defined(_WIN32)
#include <dlfcn.h>
#endif

namespace gnumon::control {

InputLatencyTracker::InputLatencyTracker() = default;

InputLatencyTracker::~InputLatencyTracker() {
    Stop();
}

bool InputLatencyTracker::Start() {
    if (running_.load()) return true;

    inputFds_.clear();

    // Scan /dev/input/event* (optional, requires root or input group)
    std::filesystem::path inputDir("/dev/input");
    if (std::filesystem::exists(inputDir)) {
        try {
            for (const auto& entry : std::filesystem::directory_iterator(inputDir)) {
                std::string filename = entry.path().filename().string();
                if (filename.rfind("event", 0) == 0) {
                    int fd = open(entry.path().c_str(), O_RDONLY | O_NONBLOCK);
                    if (fd >= 0) {
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
        } catch (...) {}
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

bool InputLatencyTracker::ConsumeInGameHudHotkeyToggle() {
    return inGameHudToggleTriggered_.exchange(false, std::memory_order_acq_rel);
}

bool InputLatencyTracker::ConsumeOverlayHotkeyToggle() {
    return overlayToggleTriggered_.exchange(false, std::memory_order_acq_rel);
}

bool InputLatencyTracker::ConsumeRecordHotkeyToggle() {
    return recordToggleTriggered_.exchange(false, std::memory_order_acq_rel);
}

bool InputLatencyTracker::ConsumeMiniHudHotkeyToggle() {
    return miniHudToggleTriggered_.exchange(false, std::memory_order_acq_rel);
}

void InputLatencyTracker::SetHotkeys(const std::string& inGameHud, const std::string& record,
                                     const std::string& overlay, const std::string& miniHud)
{
    if (!inGameHud.empty()) keyNameInGameHud_ = inGameHud;
    if (!record.empty()) keyNameRecord_ = record;
    if (!overlay.empty()) keyNameOverlay_ = overlay;
    if (!miniHud.empty()) keyNameMiniHud_ = miniHud;

    auto toEvdev = [](const std::string& s, uint16_t def) -> uint16_t {
        if (s == "F1") return 59;
        if (s == "F2") return 60;
        if (s == "F3") return 61;
        if (s == "F4") return 62;
        if (s == "F5") return 63;
        if (s == "F6") return 64;
        if (s == "F7") return 65;
        if (s == "F8") return 66;
        if (s == "F9") return 67;
        if (s == "F10") return 68;
        if (s == "F11") return 87;
        if (s == "F12") return 88;
        return def;
    };
    evdevInGameHud_ = toEvdev(keyNameInGameHud_, 67);
    evdevRecord_ = toEvdev(keyNameRecord_, 68);
    evdevOverlay_ = toEvdev(keyNameOverlay_, 87);
    evdevMiniHud_ = toEvdev(keyNameMiniHud_, 88);
}

void InputLatencyTracker::SetHotkeyKeycode(uint16_t code) {
    evdevRecord_ = code;
}

void InputLatencyTracker::WorkerLoop() {
    void* x11Lib = nullptr;
#if !defined(_WIN32)
    x11Lib = dlopen("libX11.so.6", RTLD_LAZY);
    if (!x11Lib) x11Lib = dlopen("libX11.so", RTLD_LAZY);
#endif

    typedef void* (*XOpenDisplay_fn)(const char*);
    typedef int (*XCloseDisplay_fn)(void*);
    typedef int (*XQueryKeymap_fn)(void*, char[32]);
    typedef unsigned long (*XStringToKeysym_fn)(const char*);
    typedef unsigned char (*XKeysymToKeycode_fn)(void*, unsigned long);

    XOpenDisplay_fn pXOpenDisplay = nullptr;
    XCloseDisplay_fn pXCloseDisplay = nullptr;
    XQueryKeymap_fn pXQueryKeymap = nullptr;
    XStringToKeysym_fn pXStringToKeysym = nullptr;
    XKeysymToKeycode_fn pXKeysymToKeycode = nullptr;

    if (x11Lib) {
        pXOpenDisplay = (XOpenDisplay_fn)dlsym(x11Lib, "XOpenDisplay");
        pXCloseDisplay = (XCloseDisplay_fn)dlsym(x11Lib, "XCloseDisplay");
        pXQueryKeymap = (XQueryKeymap_fn)dlsym(x11Lib, "XQueryKeymap");
        pXStringToKeysym = (XStringToKeysym_fn)dlsym(x11Lib, "XStringToKeysym");
        pXKeysymToKeycode = (XKeysymToKeycode_fn)dlsym(x11Lib, "XKeysymToKeycode");
    }

    void* dpy = nullptr;
    if (pXOpenDisplay) {
        dpy = pXOpenDisplay(nullptr);
    }

    auto getKc = [&](const std::string& name, unsigned long defSym) -> uint8_t {
        if (!dpy || !pXKeysymToKeycode) return 0;
        unsigned long sym = defSym;
        if (pXStringToKeysym && !name.empty()) {
            unsigned long s = pXStringToKeysym(name.c_str());
            if (s != 0) sym = s;
        }
        return pXKeysymToKeycode(dpy, sym);
    };

    uint8_t kcInGame = 0, kcRec = 0, kcOverlay = 0, kcMini = 0;
    if (dpy) {
        kcInGame = getKc(keyNameInGameHud_, 0xffc6 /* XK_F9 */);
        kcRec = getKc(keyNameRecord_, 0xffc7 /* XK_F10 */);
        kcOverlay = getKc(keyNameOverlay_, 0xffc8 /* XK_F11 */);
        kcMini = getKc(keyNameMiniHud_, 0xffc9 /* XK_F12 */);
    }

    bool wasInGame = false, wasRec = false, wasOverlay = false, wasMini = false;
    uint64_t lastInGameNs = 0;
    uint64_t lastRecNs = 0;
    uint64_t lastOverlayNs = 0;
    uint64_t lastMiniNs = 0;
    std::vector<pollfd> pFds;
    for (int fd : inputFds_) {
        pFds.push_back({ fd, POLLIN, 0 });
    }

    while (running_.load(std::memory_order_relaxed)) {
        // 1. Global X11 / Xwayland keyboard polling
        if (dpy && pXQueryKeymap) {
            char keys[32]{};
            pXQueryKeymap(dpy, keys);

            auto isDown = [&](uint8_t kc) -> bool {
                if (kc == 0) return false;
                return (keys[kc / 8] & (1 << (kc % 8))) != 0;
            };

            bool downInGame = isDown(kcInGame);
            if (downInGame && !wasInGame) {
                uint64_t nowNs = common::Clock::GetTimestampNs();
                if (nowNs >= lastInGameNs + 300'000'000ULL) {
                    inGameHudToggleTriggered_.store(true, std::memory_order_release);
                    lastInGameNs = nowNs;
                }
            }
            wasInGame = downInGame;

            bool downRec = isDown(kcRec);
            if (downRec && !wasRec) {
                uint64_t nowNs = common::Clock::GetTimestampNs();
                if (nowNs >= lastRecNs + 500'000'000ULL) {
                    recordToggleTriggered_.store(true, std::memory_order_release);
                    lastRecNs = nowNs;
                }
            }
            wasRec = downRec;

            bool downOverlay = isDown(kcOverlay);
            if (downOverlay && !wasOverlay) {
                uint64_t nowNs = common::Clock::GetTimestampNs();
                if (nowNs >= lastOverlayNs + 300'000'000ULL) {
                    overlayToggleTriggered_.store(true, std::memory_order_release);
                    lastOverlayNs = nowNs;
                }
            }
            wasOverlay = downOverlay;

            bool downMini = isDown(kcMini);
            if (downMini && !wasMini) {
                uint64_t nowNs = common::Clock::GetTimestampNs();
                if (nowNs >= lastMiniNs + 300'000'000ULL) {
                    miniHudToggleTriggered_.store(true, std::memory_order_release);
                    lastMiniNs = nowNs;
                }
            }
            wasMini = downMini;
        }

        // 2. Evdev input device polling (if /dev/input permissions available)
        if (!pFds.empty()) {
            int ret = poll(pFds.data(), pFds.size(), 30);
            if (ret > 0) {
                for (auto& pfd : pFds) {
                    if (pfd.revents & POLLIN) {
                        struct input_event ev[16];
                        ssize_t n = read(pfd.fd, ev, sizeof(ev));
                        if (n >= static_cast<ssize_t>(sizeof(struct input_event))) {
                            size_t count = n / sizeof(struct input_event);
                            for (size_t i = 0; i < count; ++i) {
                                if (ev[i].type == EV_KEY && ev[i].value == 1) {
                                    uint64_t nowNs = common::Clock::GetTimestampNs();
                                    if (ev[i].code == BTN_LEFT || ev[i].code == BTN_RIGHT || ev[i].code == BTN_MIDDLE) {
                                        lastClickTimestampNs_.store(nowNs, std::memory_order_release);
                                    }
                                    if (ev[i].code == evdevInGameHud_) {
                                        if (nowNs >= lastInGameNs + 300'000'000ULL) {
                                            inGameHudToggleTriggered_.store(true, std::memory_order_release);
                                            lastInGameNs = nowNs;
                                        }
                                    }
                                    if (ev[i].code == evdevRecord_) {
                                        if (nowNs >= lastRecNs + 500'000'000ULL) {
                                            recordToggleTriggered_.store(true, std::memory_order_release);
                                            lastRecNs = nowNs;
                                        }
                                    }
                                    if (ev[i].code == evdevOverlay_) {
                                        if (nowNs >= lastOverlayNs + 300'000'000ULL) {
                                            overlayToggleTriggered_.store(true, std::memory_order_release);
                                            lastOverlayNs = nowNs;
                                        }
                                    }
                                    if (ev[i].code == evdevMiniHud_) {
                                        if (nowNs >= lastMiniNs + 300'000'000ULL) {
                                            miniHudToggleTriggered_.store(true, std::memory_order_release);
                                            lastMiniNs = nowNs;
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(30));
        }
    }

    if (dpy && pXCloseDisplay) {
        pXCloseDisplay(dpy);
    }
#if !defined(_WIN32)
    if (x11Lib) {
        dlclose(x11Lib);
    }
#endif
}

} // namespace gnumon::control
