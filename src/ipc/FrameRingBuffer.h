#pragma once

#include <cstdint>
#include <atomic>
#include <string>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/file.h>
#include <fcntl.h>
#include <unistd.h>
#include <cstring>
#include <fstream>

namespace gnumon::ipc {

constexpr size_t RING_BUFFER_CAPACITY = 2048; // Must be power of 2
constexpr size_t RING_BUFFER_MASK = RING_BUFFER_CAPACITY - 1;

struct alignas(64) FrameEvent {
    uint32_t processId = 0;
    uint32_t frameId = 0;
    uint64_t cpuStartTimestampNs = 0;
    uint64_t presentStartTimestampNs = 0;
    uint64_t presentDurationNs = 0;
    uint64_t frameTimeNs = 0; // delta from previous present
    uint64_t swapChain = 0;
    uint32_t presentMode = 0;
    uint32_t flags = 0;

    // GPU timing & presentation metrics
    uint64_t gpuStartTimestampNs = 0;
    uint64_t gpuDurationNs = 0;      // PM_METRIC_GPU_TIME
    uint64_t gpuBusyNs = 0;          // PM_METRIC_GPU_BUSY
    uint64_t gpuWaitNs = 0;          // PM_METRIC_GPU_WAIT
    uint64_t displayTimestampNs = 0; // PM_METRIC_UNTIL_DISPLAYED
    uint32_t dropped = 0;            // PM_METRIC_DROPPED_FRAMES
    uint32_t frameType = 2;          // PM_FRAME_TYPE_APPLICATION (2), AMD_AFMF (100), INTEL_XEFG (50)

    // Pipeline State Object (PSO) & Shader Compilation Tracking
    uint32_t psoCompileCount = 0;
    uint32_t graphicsRuntime = 3;    // PM_GRAPHICS_RUNTIME_VULKAN (3), PM_GRAPHICS_RUNTIME_OPENGL (4)
    uint64_t psoCompileDurationNs = 0;
};

struct alignas(64) TelemetrySnapshot {
    float gpuUtil = 0.0f;
    float gpuTemp = 0.0f;
    float gpuPower = 0.0f;
    float gpuFreq = 0.0f;
    float vramUsedGb = 0.0f;
    float vramTotalGb = 0.0f;
    float cpuUtil = 0.0f;
    float cpuTemp = 0.0f;
    float cpuPower = 0.0f;
    float cpuFreq = 0.0f;
    float gpuVoltage = 0.0f;
    float gpuFanSpeed = 0.0f;
    char gpuName[64]{};
    char cpuName[64]{};
    uint32_t valid = 0;
    char pad[12]{};
};

struct alignas(64) SharedRingHeader {
    std::atomic<uint64_t> writeIndex{0};
    char pad1[64 - sizeof(std::atomic<uint64_t>)];
    std::atomic<uint64_t> readIndex{0};
    char pad2[64 - sizeof(std::atomic<uint64_t>)];
    uint32_t processId = 0;
    uint32_t magic = 0x474E554D; // "GNUM"
    std::atomic<uint32_t> controlFlags{0}; // Bit 0: RecordingActive, Bit 1: OverlayEnabled
    char processName[52]{};
    TelemetrySnapshot telemetry{};
    FrameEvent events[RING_BUFFER_CAPACITY];
};

class FrameRingProducer {
public:
    FrameRingProducer() = default;
    ~FrameRingProducer() {
        Close();
    }

    bool Open(uint32_t pid) {
        Close();
        processId_ = pid;
        shmName_ = "/gnumon_ring_" + std::to_string(pid);

        // Remove any old segment
        shm_unlink(shmName_.c_str());

        int fd = shm_open(shmName_.c_str(), O_CREAT | O_RDWR, 0666);
        if (fd < 0) return false;

        // Take exclusive advisory lock so consumers know the producer is alive
        flock(fd, LOCK_EX | LOCK_NB);

        if (ftruncate(fd, sizeof(SharedRingHeader)) < 0) {
            flock(fd, LOCK_UN);
            close(fd);
            return false;
        }

        void* ptr = mmap(nullptr, sizeof(SharedRingHeader), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
        if (ptr == MAP_FAILED) {
            flock(fd, LOCK_UN);
            close(fd);
            return false;
        }

        fd_ = fd;
        ring_ = static_cast<SharedRingHeader*>(ptr);
        std::memset(static_cast<void*>(ring_), 0, sizeof(SharedRingHeader));
        ring_->magic = 0x474E554D;
        ring_->processId = pid;
        ring_->writeIndex.store(0, std::memory_order_relaxed);
        ring_->readIndex.store(0, std::memory_order_relaxed);

        // Detect own process comm name
        std::ifstream commFile("/proc/self/comm");
        if (commFile.is_open()) {
            std::string comm;
            if (std::getline(commFile, comm) && !comm.empty()) {
                std::strncpy(ring_->processName, comm.c_str(), sizeof(ring_->processName) - 1);
            }
        }

        return true;
    }

    void Push(const FrameEvent& event) {
        if (!ring_) return;

        uint64_t currentWrite = ring_->writeIndex.load(std::memory_order_relaxed);
        uint64_t currentRead = ring_->readIndex.load(std::memory_order_acquire);

        // If ring is full, advance read pointer (drop oldest frame)
        if (currentWrite - currentRead >= RING_BUFFER_CAPACITY) {
            ring_->readIndex.store(currentWrite - RING_BUFFER_CAPACITY + 1, std::memory_order_release);
        }

        ring_->events[currentWrite & RING_BUFFER_MASK] = event;
        ring_->writeIndex.store(currentWrite + 1, std::memory_order_release);
    }

    bool IsRecordingActive() const {
        return ring_ && (ring_->controlFlags.load(std::memory_order_acquire) & 1);
    }
    bool IsOverlayEnabled() const {
        return ring_ && (ring_->controlFlags.load(std::memory_order_acquire) & 2);
    }
    void SetRecordingActive(bool active) {
        if (!ring_) return;
        if (active) ring_->controlFlags.fetch_or(1, std::memory_order_release);
        else ring_->controlFlags.fetch_and(~1, std::memory_order_release);
    }
    void SetOverlayEnabled(bool enabled) {
        if (!ring_) return;
        if (enabled) ring_->controlFlags.fetch_or(2, std::memory_order_release);
        else ring_->controlFlags.fetch_and(~2, std::memory_order_release);
    }

    bool ReadTelemetry(TelemetrySnapshot& out) const {
        if (!ring_) return false;
        out = ring_->telemetry;
        return out.valid != 0;
    }

    void WriteTelemetry(const TelemetrySnapshot& snap) {
        if (!ring_) return;
        ring_->telemetry = snap;
    }

    void Close() {
        if (ring_) {
            munmap(ring_, sizeof(SharedRingHeader));
            ring_ = nullptr;
            if (!shmName_.empty()) {
                shm_unlink(shmName_.c_str());
            }
        }
        if (fd_ >= 0) {
            flock(fd_, LOCK_UN);
            close(fd_);
            fd_ = -1;
        }
        processId_ = 0;
        shmName_.clear();
    }

private:
    uint32_t processId_ = 0;
    std::string shmName_;
    int fd_ = -1;
    SharedRingHeader* ring_ = nullptr;
};

class FrameRingConsumer {
public:
    FrameRingConsumer() = default;
    ~FrameRingConsumer() {
        Close();
    }

    bool Open(uint32_t pid) {
        Close();
        processId_ = pid;
        shmName_ = "/gnumon_ring_" + std::to_string(pid);

        int fd = shm_open(shmName_.c_str(), O_RDWR, 0666);
        if (fd < 0) return false;

        void* ptr = mmap(nullptr, sizeof(SharedRingHeader), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
        close(fd);

        if (ptr == MAP_FAILED) return false;

        ring_ = static_cast<SharedRingHeader*>(ptr);
        if (ring_->magic != 0x474E554D) {
            munmap(ring_, sizeof(SharedRingHeader));
            ring_ = nullptr;
            return false;
        }

        uint64_t w = ring_->writeIndex.load(std::memory_order_acquire);
        localReadIndex_ = (w > 32) ? (w - 32) : 0;
        return true;
    }

    bool Pop(FrameEvent& event) {
        if (!ring_) return false;

        uint64_t currentWrite = ring_->writeIndex.load(std::memory_order_acquire);
        if (localReadIndex_ >= currentWrite) {
            return false; // Empty or up to date
        }

        if (currentWrite - localReadIndex_ > RING_BUFFER_CAPACITY) {
            localReadIndex_ = currentWrite - RING_BUFFER_CAPACITY;
        }

        event = ring_->events[localReadIndex_ & RING_BUFFER_MASK];
        localReadIndex_++;
        return true;
    }

    // Read latest event without advancing read pointer
    bool PeekLatest(FrameEvent& event) {
        if (!ring_) return false;

        uint64_t currentWrite = ring_->writeIndex.load(std::memory_order_acquire);
        if (currentWrite == 0) return false;

        event = ring_->events[(currentWrite - 1) & RING_BUFFER_MASK];
        return true;
    }

    uint32_t GetProcessId() const {
        return processId_;
    }

    const char* GetProcessName() const {
        return ring_ ? ring_->processName : "";
    }

    void Close() {
        if (ring_) {
            munmap(ring_, sizeof(SharedRingHeader));
            ring_ = nullptr;
        }
        processId_ = 0;
        shmName_.clear();
        localReadIndex_ = 0;
    }

    bool IsConnected() const {
        return ring_ != nullptr;
    }

    void SetRecordingActive(bool active) {
        if (!ring_) return;
        if (active) ring_->controlFlags.fetch_or(1, std::memory_order_release);
        else ring_->controlFlags.fetch_and(~1, std::memory_order_release);
    }

    void SetOverlayEnabled(bool enabled) {
        if (!ring_) return;
        if (enabled) ring_->controlFlags.fetch_or(2, std::memory_order_release);
        else ring_->controlFlags.fetch_and(~2, std::memory_order_release);
    }

    bool IsRecordingActive() const {
        return ring_ && (ring_->controlFlags.load(std::memory_order_acquire) & 1);
    }

    bool IsOverlayEnabled() const {
        return ring_ && (ring_->controlFlags.load(std::memory_order_acquire) & 2);
    }

    void WriteTelemetry(const TelemetrySnapshot& snap) {
        if (!ring_) return;
        ring_->telemetry = snap;
    }

private:
    uint32_t processId_ = 0;
    std::string shmName_;
    SharedRingHeader* ring_ = nullptr;
    uint64_t localReadIndex_ = 0;
};

} // namespace gnumon::ipc
