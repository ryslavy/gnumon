#include "../ipc/FrameRingBuffer.h"
#include "../common/Clock.h"
#include "GlOverlayRenderer.h"
#include <dlfcn.h>
#include <atomic>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <linux/input.h>
#include <vector>
#include <deque>
#include <algorithm>
#include <fstream>
#include <cmath>
#include <iostream>

namespace {

using namespace gnumon::layer;

gnumon::ipc::FrameRingProducer g_glProducer;
std::atomic<uint32_t> g_glFrameId{0};
std::atomic<uint64_t> g_glLastPresentNs{0};
std::atomic<bool> g_glInit{false};

gnumon::layer::GlOverlayRenderer g_glOverlay;

struct GlFpsHistory {
    std::deque<double> fps;
    std::deque<float> frametimes;
    uint64_t lastDisplayNs = 0;
};
GlFpsHistory g_glFpsHistory;
std::mutex g_glHudMutex;

static int GetConfiguredHudCorner() {
    const char* envCorner = getenv("GNUMON_CORNER");
    if (envCorner) {
        std::string s(envCorner);
        if (s == "1" || s == "TR" || s == "top-right") return 1;
        if (s == "2" || s == "BL" || s == "bottom-left") return 2;
        if (s == "3" || s == "BR" || s == "bottom-right") return 3;
        return 0;
    }
    const char* home = getenv("HOME");
    if (home) {
        std::string cfgPath = std::string(home) + "/.config/gnumon/config.ini";
        std::ifstream file(cfgPath);
        if (file.is_open()) {
            std::string line;
            while (std::getline(file, line)) {
                if (line.rfind("inGameHudCorner", 0) == 0) {
                    auto pos = line.find('=');
                    if (pos != std::string::npos) {
                        try { return std::stoi(line.substr(pos + 1)); } catch (...) {}
                    }
                }
            }
        }
    }
    return 0; // Top-Left default
}

static bool GetConfiguredHudDefault() {
    const char* envOverlay = getenv("GNUMON_OVERLAY");
    if (envOverlay) {
        return (std::string(envOverlay) != "0");
    }
    const char* home = getenv("HOME");
    if (home) {
        std::string cfgPath = std::string(home) + "/.config/gnumon/config.ini";
        std::ifstream file(cfgPath);
        if (file.is_open()) {
            std::string line;
            while (std::getline(file, line)) {
                if (line.rfind("inGameHudEnabled", 0) == 0) {
                    auto pos = line.find('=');
                    if (pos != std::string::npos) {
                        std::string val = line.substr(pos + 1);
                        return (val == "true" || val == "1");
                    }
                }
            }
        }
    }
    return true; // Default enabled for in-game HUD
}

static int GetConfiguredHudPreset() {
    const char* home = getenv("HOME");
    if (home) {
        std::string cfgPath = std::string(home) + "/.config/gnumon/config.ini";
        std::ifstream file(cfgPath);
        if (file.is_open()) {
            std::string line;
            while (std::getline(file, line)) {
                if (line.rfind("inGameHudPreset", 0) == 0 || line.rfind("selectedPreset", 0) == 0) {
                    auto pos = line.find('=');
                    if (pos != std::string::npos) {
                        try {
                            int p = std::stoi(line.substr(pos + 1));
                            if (p >= 0 && p <= 2) return p;
                            if (p == 3) return 2;
                        } catch (...) {}
                    }
                }
            }
        }
    }
    return 1; // Standard (Oscilloscope) default
}

static bool g_enableOverlay = GetConfiguredHudDefault();
static int g_hudCorner = GetConfiguredHudCorner();

void EnsureInit() {
    if (!g_glInit.exchange(true)) {
        g_glProducer.Open(getpid());
        g_glOverlay.SetPreset(GetConfiguredHudPreset());
    }
}

static void CheckInGameHotkeys(uint64_t nowNs) {
    static uint64_t lastCheckNs = 0;
    if (nowNs < lastCheckNs + 33'000'000) {
        return; // Check at most ~30 Hz
    }
    lastCheckNs = nowNs;

    static std::vector<int> evdevFds;
    static bool evdevScanned = false;
    if (!evdevScanned) {
        evdevScanned = true;
        DIR* dir = opendir("/dev/input");
        if (dir) {
            struct dirent* ent;
            while ((ent = readdir(dir)) != nullptr) {
                if (strncmp(ent->d_name, "event", 5) == 0) {
                    std::string path = std::string("/dev/input/") + ent->d_name;
                    int fd = open(path.c_str(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
                    if (fd >= 0) {
                        evdevFds.push_back(fd);
                    }
                }
            }
            closedir(dir);
        }
    }

    for (int efd : evdevFds) {
        struct input_event iev[8];
        ssize_t n = read(efd, iev, sizeof(iev));
        if (n > 0) {
            size_t count = n / sizeof(struct input_event);
            for (size_t i = 0; i < count; ++i) {
                if (iev[i].type == EV_KEY && iev[i].value == 1) {
                    if (iev[i].code == 66 /* KEY_F8 */) {
                        int nextPreset = (g_glOverlay.GetPreset() + 1) % 3;
                        g_glOverlay.SetPreset(nextPreset);
                        const char* presetNames[] = {"Compact", "Standard (Oscilloscope)", "Detailed"};
                        g_glOverlay.TriggerToast("Preset Changed", presetNames[nextPreset], 2.5f);
                    } else if (iev[i].code == 67 /* KEY_F9 */) {
                        g_enableOverlay = !g_enableOverlay;
                        g_glProducer.SetOverlayEnabled(g_enableOverlay);
                        g_glOverlay.TriggerToast("In-Game Overlay", g_enableOverlay ? "ENABLED" : "DISABLED", 2.0f);
                    } else if (iev[i].code == 68 /* KEY_F10 */) {
                        bool newRec = !g_glProducer.IsRecordingActive();
                        g_glProducer.SetRecordingActive(newRec);
                        g_glOverlay.TriggerToast(newRec ? "Benchmark Capture" : "Capture Saved",
                                                newRec ? "RECORDING STARTED" : "CSV BENCHMARK SAVED", 3.0f);
                    }
                }
            }
        }
    }
}

void PreSwapHook(uint64_t nowNs) {
    EnsureInit();
    CheckInGameHotkeys(nowNs);

    // Query active OpenGL Viewport dimensions
    static auto p_glGetIntegerv = reinterpret_cast<void (*)(GLenum, GLint*)>(dlsym(RTLD_DEFAULT, "glGetIntegerv"));
    if (!p_glGetIntegerv) return;

    GLint vp[4]{0, 0, 0, 0};
    p_glGetIntegerv(GL_VIEWPORT, vp);
    int screenW = vp[2];
    int screenH = vp[3];
    if (screenW <= 0 || screenH <= 0) return;

    // Calculate rolling frametime metrics
    double presentFps = 0.0;
    double dispFps = 0.0;
    double lowFps = 0.0;
    double ftMs = 0.0;
    double latMs = 0.0;
    double animErrMs = 0.0;

    {
        std::lock_guard<std::mutex> lock(g_glHudMutex);
        if (!g_glFpsHistory.fps.empty()) {
            presentFps = g_glFpsHistory.fps.back();
            ftMs = (presentFps > 0) ? (1000.0 / presentFps) : 0.0;
            std::vector<double> sorted(g_glFpsHistory.fps.begin(), g_glFpsHistory.fps.end());
            std::sort(sorted.begin(), sorted.end());
            size_t idx = static_cast<size_t>(std::floor(sorted.size() * 0.01));
            lowFps = sorted[std::min(idx, sorted.size() - 1)];
        }
        dispFps = presentFps;
        if (g_glFpsHistory.lastDisplayNs > 0 && nowNs > g_glFpsHistory.lastDisplayNs) {
            uint64_t deltaDispNs = nowNs - g_glFpsHistory.lastDisplayNs;
            dispFps = 1'000'000'000.0 / static_cast<double>(deltaDispNs);
        }
        latMs = ftMs;
    }

    g_glOverlay.AddFrametimeSample(static_cast<float>(ftMs));

    // Render in-game HUD overlay directly into OpenGL context before buffer swap
    bool active = g_enableOverlay || g_glOverlay.HasActiveToast();
    g_glOverlay.Render(screenW, screenH, g_hudCorner,
                       presentFps, dispFps, lowFps,
                       ftMs, latMs, animErrMs,
                       g_glProducer.IsRecordingActive(),
                       nullptr, active);
}

void RecordGlFrame(uint64_t startNs, uint64_t endNs, uint64_t drawableHandle) {
    EnsureInit();

    uint64_t last = g_glLastPresentNs.exchange(startNs, std::memory_order_acq_rel);
    uint64_t frameTimeNs = (last > 0 && startNs > last) ? (startNs - last) : 0;
    double currentFps = (frameTimeNs > 0) ? (1'000'000'000.0 / static_cast<double>(frameTimeNs)) : 0.0;

    {
        std::lock_guard<std::mutex> lock(g_glHudMutex);
        g_glFpsHistory.fps.push_back(currentFps);
        if (g_glFpsHistory.fps.size() > 60) g_glFpsHistory.fps.pop_front();
        g_glFpsHistory.lastDisplayNs = endNs;
    }

    gnumon::ipc::FrameEvent event{};
    event.processId = static_cast<uint32_t>(getpid());
    event.frameId = ++g_glFrameId;
    event.cpuStartTimestampNs = startNs;
    event.presentStartTimestampNs = startNs;
    event.presentDurationNs = endNs - startNs;
    event.frameTimeNs = frameTimeNs;
    event.swapChain = drawableHandle;
    event.presentMode = 0; // Composed
    event.flags = 0;
    event.gpuStartTimestampNs = startNs;
    event.gpuDurationNs = endNs - startNs;
    event.gpuBusyNs = endNs - startNs;
    event.displayTimestampNs = endNs;
    event.dropped = 0;

    g_glProducer.Push(event);
}

} // namespace

extern "C" {

// GLX SwapBuffers intercept
typedef void (*PFN_glXSwapBuffers)(void* dpy, unsigned long drawable);
void glXSwapBuffers(void* dpy, unsigned long drawable) {
    static PFN_glXSwapBuffers real_glXSwapBuffers = []() {
        return reinterpret_cast<PFN_glXSwapBuffers>(dlsym(RTLD_NEXT, "glXSwapBuffers"));
    }();

    uint64_t start = gnumon::common::Clock::GetTimestampNs();
    PreSwapHook(start);

    if (real_glXSwapBuffers) {
        real_glXSwapBuffers(dpy, drawable);
    }
    uint64_t end = gnumon::common::Clock::GetTimestampNs();

    RecordGlFrame(start, end, static_cast<uint64_t>(drawable));
}

// EGL SwapBuffers intercept
typedef int (*PFN_eglSwapBuffers)(void* dpy, void* surface);
int eglSwapBuffers(void* dpy, void* surface) {
    static PFN_eglSwapBuffers real_eglSwapBuffers = []() {
        return reinterpret_cast<PFN_eglSwapBuffers>(dlsym(RTLD_NEXT, "eglSwapBuffers"));
    }();

    uint64_t start = gnumon::common::Clock::GetTimestampNs();
    PreSwapHook(start);

    int result = 0;
    if (real_eglSwapBuffers) {
        result = real_eglSwapBuffers(dpy, surface);
    }
    uint64_t end = gnumon::common::Clock::GetTimestampNs();

    RecordGlFrame(start, end, reinterpret_cast<uint64_t>(surface));
    return result;
}

} // extern "C"
