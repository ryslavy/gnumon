#include "../ipc/FrameRingBuffer.h"
#include "../common/Clock.h"
#include <dlfcn.h>
#include <atomic>
#include <unistd.h>
#include <iostream>

namespace {

gnumon::ipc::FrameRingProducer g_glProducer;
std::atomic<uint32_t> g_glFrameId{0};
std::atomic<uint64_t> g_glLastPresentNs{0};
std::atomic<bool> g_glInit{false};

void EnsureInit() {
    if (!g_glInit.exchange(true)) {
        g_glProducer.Open(getpid());
    }
}

void RecordGlFrame(uint64_t startNs, uint64_t endNs, uint64_t drawableHandle) {
    EnsureInit();

    uint64_t last = g_glLastPresentNs.exchange(startNs, std::memory_order_acq_rel);
    uint64_t frameTimeNs = (last > 0 && startNs > last) ? (startNs - last) : 0;

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
    int result = 0;
    if (real_eglSwapBuffers) {
        result = real_eglSwapBuffers(dpy, surface);
    }
    uint64_t end = gnumon::common::Clock::GetTimestampNs();

    RecordGlFrame(start, end, reinterpret_cast<uint64_t>(surface));
    return result;
}

} // extern "C"
