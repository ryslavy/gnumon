#include "../ipc/FrameRingBuffer.h"
#include "../common/Clock.h"
#include "GlOverlayRenderer.h"
#include "InGameHotkeyManager.h"
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
static bool g_lastProducerOverlay = GetConfiguredHudDefault();
static int g_hudCorner = GetConfiguredHudCorner();

void EnsureInit() {
    if (!g_glInit.exchange(true)) {
        g_glProducer.Open(getpid());
        if (g_enableOverlay) {
            g_glProducer.SetOverlayEnabled(true);
        }
        g_glOverlay.SetPreset(GetConfiguredHudPreset());
    }
}

static gnumon::layer::InGameHotkeyManager g_glHotkeyManager;

static void CheckInGameHotkeys(uint64_t nowNs) {
    g_glOverlay.CheckReloadConfig(nowNs);
    g_glHotkeyManager.UpdateChords(g_glOverlay.GetHotkeyOverlay(),
                                  g_glOverlay.GetHotkeyPresetCycle(),
                                  g_glOverlay.GetHotkeyCapture());

    auto ev = g_glHotkeyManager.Poll(nowNs);

    if (ev.cyclePreset) {
        int nextPreset = (g_glOverlay.GetPreset() + 1) % 3;
        g_glOverlay.SetPreset(nextPreset);
        const char* presetNames[] = {"Compact", "Standard (Oscilloscope)", "Detailed"};
        g_glOverlay.TriggerToast("Preset Changed", presetNames[nextPreset], 2.5f);
    }
    if (ev.toggleOverlay) {
        g_enableOverlay = !g_enableOverlay;
        g_glProducer.SetOverlayEnabled(g_enableOverlay);
        g_lastProducerOverlay = g_enableOverlay;
        g_glOverlay.TriggerToast("In-Game Overlay", g_enableOverlay ? "ENABLED" : "DISABLED", 2.0f);
        if (getenv("GNUMON_DEBUG") || getenv("GNUMON_OVERLAY")) {
            fprintf(stderr, "[gnumon-gl] In-game hotkey pressed! In-Game HUD: %s\n",
                    g_enableOverlay ? "ON" : "OFF");
        }
    }
    if (ev.toggleCapture) {
        bool newRec = !g_glProducer.IsRecordingActive();
        g_glProducer.SetRecordingActive(newRec);
        g_glOverlay.TriggerToast(newRec ? "Benchmark Capture" : "Capture Saved",
                                newRec ? "RECORDING STARTED" : "CSV BENCHMARK SAVED", 3.0f);
        if (getenv("GNUMON_DEBUG") || getenv("GNUMON_OVERLAY")) {
            fprintf(stderr, "[gnumon-gl] In-game hotkey pressed! Capture: %s\n",
                    newRec ? "STARTING" : "STOPPED");
        }
    }
}

void PreSwapHook(uint64_t nowNs) {
    EnsureInit();
    CheckInGameHotkeys(nowNs);

    bool currentProducerOverlay = g_glProducer.IsOverlayEnabled();
    if (currentProducerOverlay != g_lastProducerOverlay) {
        g_enableOverlay = currentProducerOverlay;
        g_lastProducerOverlay = currentProducerOverlay;
    }

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
    event.graphicsRuntime = 4; // PM_GRAPHICS_RUNTIME_OPENGL
    event.gpuStartTimestampNs = startNs;
    event.gpuDurationNs = endNs - startNs;
    event.gpuBusyNs = endNs - startNs;
    event.displayTimestampNs = endNs;
    event.dropped = 0;

    g_glProducer.Push(event);
}

static bool IsVulkanLayerActiveInProcess() {
    static int cached = -1;
    if (cached != -1) return cached == 1;
    if (getenv("__GNUMON_VK_ACTIVE") != nullptr) {
        cached = 1;
        return true;
    }
    void* sym = dlsym(RTLD_DEFAULT, "gnumon_is_vulkan_layer_active");
    if (sym != nullptr) {
        cached = 1;
        return true;
    }
    cached = 0;
    return false;
}

} // namespace

extern "C" {

// Forward declarations
void glXSwapBuffers(void* dpy, unsigned long drawable);
int64_t glXSwapBuffersMscOML(void* dpy, unsigned long drawable, int64_t target_msc, int64_t divisor, int64_t remainder);
int eglSwapBuffers(void* dpy, void* surface);
int eglSwapBuffersWithDamageKHR(void* dpy, void* surface, const int* rects, int n_rects);
int eglSwapBuffersWithDamageEXT(void* dpy, void* surface, const int* rects, int n_rects);

// GLX SwapBuffers intercept
typedef void (*PFN_glXSwapBuffers)(void* dpy, unsigned long drawable);
void glXSwapBuffers(void* dpy, unsigned long drawable) {
    static PFN_glXSwapBuffers real_glXSwapBuffers = []() {
        return reinterpret_cast<PFN_glXSwapBuffers>(dlsym(RTLD_NEXT, "glXSwapBuffers"));
    }();

    if (IsVulkanLayerActiveInProcess()) {
        if (real_glXSwapBuffers) real_glXSwapBuffers(dpy, drawable);
        return;
    }

    uint64_t start = gnumon::common::Clock::GetTimestampNs();
    PreSwapHook(start);

    if (real_glXSwapBuffers) {
        real_glXSwapBuffers(dpy, drawable);
    }
    uint64_t end = gnumon::common::Clock::GetTimestampNs();

    RecordGlFrame(start, end, static_cast<uint64_t>(drawable));
}

// GLX SwapBuffersMscOML intercept
typedef int64_t (*PFN_glXSwapBuffersMscOML)(void* dpy, unsigned long drawable, int64_t target_msc, int64_t divisor, int64_t remainder);
int64_t glXSwapBuffersMscOML(void* dpy, unsigned long drawable, int64_t target_msc, int64_t divisor, int64_t remainder) {
    static PFN_glXSwapBuffersMscOML real_glXSwapBuffersMscOML = []() {
        return reinterpret_cast<PFN_glXSwapBuffersMscOML>(dlsym(RTLD_NEXT, "glXSwapBuffersMscOML"));
    }();

    if (IsVulkanLayerActiveInProcess()) {
        return real_glXSwapBuffersMscOML ? real_glXSwapBuffersMscOML(dpy, drawable, target_msc, divisor, remainder) : 0;
    }

    uint64_t start = gnumon::common::Clock::GetTimestampNs();
    PreSwapHook(start);

    int64_t result = 0;
    if (real_glXSwapBuffersMscOML) {
        result = real_glXSwapBuffersMscOML(dpy, drawable, target_msc, divisor, remainder);
    }
    uint64_t end = gnumon::common::Clock::GetTimestampNs();

    RecordGlFrame(start, end, static_cast<uint64_t>(drawable));
    return result;
}

// EGL SwapBuffers intercept
typedef int (*PFN_eglSwapBuffers)(void* dpy, void* surface);
int eglSwapBuffers(void* dpy, void* surface) {
    static PFN_eglSwapBuffers real_eglSwapBuffers = []() {
        return reinterpret_cast<PFN_eglSwapBuffers>(dlsym(RTLD_NEXT, "eglSwapBuffers"));
    }();

    if (IsVulkanLayerActiveInProcess()) {
        return real_eglSwapBuffers ? real_eglSwapBuffers(dpy, surface) : 0;
    }

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

// EGL SwapBuffersWithDamageKHR intercept
typedef int (*PFN_eglSwapBuffersWithDamage)(void* dpy, void* surface, const int* rects, int n_rects);
int eglSwapBuffersWithDamageKHR(void* dpy, void* surface, const int* rects, int n_rects) {
    static PFN_eglSwapBuffersWithDamage real_eglSwapWithDamage = []() {
        return reinterpret_cast<PFN_eglSwapBuffersWithDamage>(dlsym(RTLD_NEXT, "eglSwapBuffersWithDamageKHR"));
    }();

    if (IsVulkanLayerActiveInProcess()) {
        return real_eglSwapWithDamage ? real_eglSwapWithDamage(dpy, surface, rects, n_rects) : 0;
    }

    uint64_t start = gnumon::common::Clock::GetTimestampNs();
    PreSwapHook(start);

    int result = 0;
    if (real_eglSwapWithDamage) {
        result = real_eglSwapWithDamage(dpy, surface, rects, n_rects);
    } else {
        result = eglSwapBuffers(dpy, surface);
    }
    uint64_t end = gnumon::common::Clock::GetTimestampNs();

    RecordGlFrame(start, end, reinterpret_cast<uint64_t>(surface));
    return result;
}

// EGL SwapBuffersWithDamageEXT intercept
int eglSwapBuffersWithDamageEXT(void* dpy, void* surface, const int* rects, int n_rects) {
    static PFN_eglSwapBuffersWithDamage real_eglSwapWithDamage = []() {
        return reinterpret_cast<PFN_eglSwapBuffersWithDamage>(dlsym(RTLD_NEXT, "eglSwapBuffersWithDamageEXT"));
    }();

    if (IsVulkanLayerActiveInProcess()) {
        return real_eglSwapWithDamage ? real_eglSwapWithDamage(dpy, surface, rects, n_rects) : 0;
    }

    uint64_t start = gnumon::common::Clock::GetTimestampNs();
    PreSwapHook(start);

    int result = 0;
    if (real_eglSwapWithDamage) {
        result = real_eglSwapWithDamage(dpy, surface, rects, n_rects);
    } else {
        result = eglSwapBuffers(dpy, surface);
    }
    uint64_t end = gnumon::common::Clock::GetTimestampNs();

    RecordGlFrame(start, end, reinterpret_cast<uint64_t>(surface));
    return result;
}

// glXGetProcAddress / glXGetProcAddressARB intercepts
typedef void* (*PFN_glXGetProcAddress)(const unsigned char* procName);
void* glXGetProcAddress(const unsigned char* procName) {
    static PFN_glXGetProcAddress real_glXGetProcAddress = []() {
        return reinterpret_cast<PFN_glXGetProcAddress>(dlsym(RTLD_NEXT, "glXGetProcAddress"));
    }();

    if (!procName) return nullptr;
    const char* name = reinterpret_cast<const char*>(procName);
    if (std::strcmp(name, "glXSwapBuffers") == 0) return reinterpret_cast<void*>(glXSwapBuffers);
    if (std::strcmp(name, "glXSwapBuffersMscOML") == 0) return reinterpret_cast<void*>(glXSwapBuffersMscOML);

    return real_glXGetProcAddress ? real_glXGetProcAddress(procName) : nullptr;
}

void* glXGetProcAddressARB(const unsigned char* procName) {
    static PFN_glXGetProcAddress real_glXGetProcAddressARB = []() {
        return reinterpret_cast<PFN_glXGetProcAddress>(dlsym(RTLD_NEXT, "glXGetProcAddressARB"));
    }();

    if (!procName) return nullptr;
    const char* name = reinterpret_cast<const char*>(procName);
    if (std::strcmp(name, "glXSwapBuffers") == 0) return reinterpret_cast<void*>(glXSwapBuffers);
    if (std::strcmp(name, "glXSwapBuffersMscOML") == 0) return reinterpret_cast<void*>(glXSwapBuffersMscOML);

    return real_glXGetProcAddressARB ? real_glXGetProcAddressARB(procName) : nullptr;
}

// eglGetProcAddress intercept
typedef void* (*PFN_eglGetProcAddress)(const char* procName);
void* eglGetProcAddress(const char* procName) {
    static PFN_eglGetProcAddress real_eglGetProcAddress = []() {
        return reinterpret_cast<PFN_eglGetProcAddress>(dlsym(RTLD_NEXT, "eglGetProcAddress"));
    }();

    if (!procName) return nullptr;
    if (std::strcmp(procName, "eglSwapBuffers") == 0) return reinterpret_cast<void*>(eglSwapBuffers);
    if (std::strcmp(procName, "eglSwapBuffersWithDamageKHR") == 0) return reinterpret_cast<void*>(eglSwapBuffersWithDamageKHR);
    if (std::strcmp(procName, "eglSwapBuffersWithDamageEXT") == 0) return reinterpret_cast<void*>(eglSwapBuffersWithDamageEXT);

    return real_eglGetProcAddress ? real_eglGetProcAddress(procName) : nullptr;
}

} // extern "C"
