#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <sys/mman.h>
#include <sys/file.h>
#include <fcntl.h>
#include <unistd.h>
#include <algorithm>
#include "../ipc/FrameRingBuffer.h"

namespace gnumon::common {

// Reads /proc/<pid>/status to extract the main TGID (Thread Group ID / Process ID)
inline uint32_t GetTgidForPid(uint32_t pid) {
    if (pid == 0) return 0;
    std::filesystem::path statusPath = "/proc/" + std::to_string(pid) + "/status";
    std::ifstream file(statusPath);
    if (!file.is_open()) return pid;

    std::string line;
    while (std::getline(file, line)) {
        if (line.rfind("Tgid:", 0) == 0) {
            std::istringstream iss(line.substr(5));
            uint32_t tgid = 0;
            if (iss >> tgid && tgid > 0) {
                return tgid;
            }
            break;
        }
    }
    return pid;
}

// Checks whether a gnumon ring buffer is actively held by a live process (native or inside container)
inline bool IsRingAlive(uint32_t pid) {
    if (pid == 0) return false;
    std::string name = "/gnumon_ring_" + std::to_string(pid);
    int fd = shm_open(name.c_str(), O_RDWR, 0666);
    if (fd < 0) return false;

    // Try acquiring exclusive non-blocking advisory lock
    int ret = flock(fd, LOCK_EX | LOCK_NB);
    if (ret == 0) {
        // We acquired the lock! That means NO producer is holding the lock.
        flock(fd, LOCK_UN);
        close(fd);
        // Fallback: check if /proc/<pid> exists on host (for producers that didn't lock)
        return std::filesystem::exists("/proc/" + std::to_string(pid));
    }
    close(fd);
    // If errno is EWOULDBLOCK or EAGAIN, a producer process is actively holding the lock!
    if (errno == EWOULDBLOCK || errno == EAGAIN) {
        return true;
    }
    return std::filesystem::exists("/proc/" + std::to_string(pid));
}

// Cleans up orphaned /dev/shm/gnumon_ring_* files from dead processes
inline void CleanStaleRings() {
    std::filesystem::path shmDir("/dev/shm");
    if (!std::filesystem::exists(shmDir)) return;

    for (const auto& entry : std::filesystem::directory_iterator(shmDir)) {
        std::string name = entry.path().filename().string();
        if (name.rfind("gnumon_ring_", 0) == 0) {
            std::string pidStr = name.substr(12);
            try {
                uint32_t pid = std::stoul(pidStr);
                if (pid > 0 && !IsRingAlive(pid)) {
                    shm_unlink(("/" + name).c_str());
                }
            } catch (...) {}
        }
    }
}

struct RingCandidate {
    uint32_t pid{0};
    uint64_t writeIndex{0};
    uint64_t lastTimestampNs{0};
};

// Scans /dev/shm for live running processes with an active gnumon ring buffer,
// sorting processes that are actively rendering frames to the very top.
inline std::vector<uint32_t> GetActiveRingPids() {
    CleanStaleRings();
    std::vector<RingCandidate> candidates;
    std::filesystem::path shmDir("/dev/shm");
    if (!std::filesystem::exists(shmDir)) return {};

    for (const auto& entry : std::filesystem::directory_iterator(shmDir)) {
        std::string name = entry.path().filename().string();
        if (name.rfind("gnumon_ring_", 0) == 0) {
            std::string pidStr = name.substr(12);
            try {
                uint32_t pid = std::stoul(pidStr);
                if (pid > 0 && IsRingAlive(pid)) {
                    RingCandidate cand;
                    cand.pid = pid;
                    std::string shmPath = "/" + name;
                    int fd = shm_open(shmPath.c_str(), O_RDONLY, 0666);
                    if (fd >= 0) {
                        void* ptr = mmap(nullptr, sizeof(ipc::SharedRingHeader), PROT_READ, MAP_SHARED, fd, 0);
                        if (ptr != MAP_FAILED) {
                            const auto* ring = static_cast<const ipc::SharedRingHeader*>(ptr);
                            if (ring->magic == 0x474E554D) {
                                cand.writeIndex = ring->writeIndex.load(std::memory_order_relaxed);
                                if (cand.writeIndex > 0) {
                                    uint64_t lastIdx = (cand.writeIndex - 1) & ipc::RING_BUFFER_MASK;
                                    cand.lastTimestampNs = ring->events[lastIdx].presentStartTimestampNs;
                                }
                            }
                            munmap(ptr, sizeof(ipc::SharedRingHeader));
                        }
                        close(fd);
                    }
                    candidates.push_back(cand);
                }
            } catch (...) {}
        }
    }

    std::sort(candidates.begin(), candidates.end(), [](const RingCandidate& a, const RingCandidate& b) {
        if (a.lastTimestampNs > 0 && b.lastTimestampNs > 0) {
            return a.lastTimestampNs > b.lastTimestampNs;
        }
        if (a.writeIndex > 0 && b.writeIndex == 0) return true;
        if (b.writeIndex > 0 && a.writeIndex == 0) return false;
        if (a.writeIndex != b.writeIndex) {
            return a.writeIndex > b.writeIndex;
        }
        return a.pid > b.pid;
    });

    std::vector<uint32_t> result;
    result.reserve(candidates.size());
    for (const auto& c : candidates) {
        result.push_back(c.pid);
    }
    return result;
}

inline std::string GetProcessName(uint32_t pid) {
    if (pid == 0) return "";

    // 1. Try reading /proc/<pid>/cmdline to detect Windows/Wine executables (.exe)
    std::string cmdPath = "/proc/" + std::to_string(pid) + "/cmdline";
    std::ifstream cmdFile(cmdPath, std::ios::binary);
    if (cmdFile.is_open()) {
        std::string raw((std::istreambuf_iterator<char>(cmdFile)), std::istreambuf_iterator<char>());
        if (!raw.empty()) {
            size_t start = 0;
            while (start < raw.size()) {
                size_t end = raw.find('\0', start);
                if (end == std::string::npos) end = raw.size();
                std::string arg = raw.substr(start, end - start);
                start = end + 1;
                if (arg.empty()) continue;

                std::string lower = arg;
                std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
                auto pos = lower.rfind(".exe");
                if (pos != std::string::npos) {
                    std::string exeName = arg.substr(0, pos + 4);
                    auto lastSlash = exeName.find_last_of("/\\");
                    if (lastSlash != std::string::npos) {
                        exeName = exeName.substr(lastSlash + 1);
                    }
                    if (!exeName.empty() && exeName != "wine64-preloader" && exeName != "explorer.exe") {
                        return exeName;
                    }
                }
            }
        }
    }

    // 2. Read /proc/<pid>/comm
    std::string commPath = "/proc/" + std::to_string(pid) + "/comm";
    std::ifstream commFile(commPath);
    if (commFile.is_open()) {
        std::string comm;
        if (std::getline(commFile, comm) && !comm.empty()) {
            while (!comm.empty() && (comm.back() == '\r' || comm.back() == '\n' || comm.back() == ' ')) {
                comm.pop_back();
            }
            if (!comm.empty() && comm != "MainThread" && comm.rfind("wine", 0) != 0) {
                return comm;
            }
        }
    }

    // 3. Check ring header for process name
    std::string ringPath = "/gnumon_ring_" + std::to_string(pid);
    int fd = shm_open(ringPath.c_str(), O_RDONLY, 0666);
    if (fd >= 0) {
        struct {
            uint64_t w;
            char p1[56];
            uint64_t r;
            char p2[56];
            uint32_t pid;
            uint32_t magic;
            uint32_t flags;
            char processName[52];
        } hdr{};
        if (read(fd, &hdr, sizeof(hdr)) == sizeof(hdr) && hdr.magic == 0x474E554D) {
            if (hdr.processName[0] != '\0') {
                close(fd);
                std::string pName(hdr.processName);
                if (pName != "MainThread") {
                    return pName;
                }
            }
        }
        close(fd);
    }

    // 4. Fallback: return comm if exists
    if (commFile.is_open()) {
        std::string comm;
        commFile.clear();
        commFile.seekg(0);
        if (std::getline(commFile, comm) && !comm.empty()) {
            while (!comm.empty() && (comm.back() == '\r' || comm.back() == '\n' || comm.back() == ' ')) {
                comm.pop_back();
            }
            if (!comm.empty()) return comm;
        }
    }

    return "Game";
}

struct ProcessInfo {
    uint32_t pid{0};
    std::string name;
    std::string windowTitle;
    bool isActiveGame{false};
};

inline std::vector<ProcessInfo> GetRunningProcesses() {
    std::vector<ProcessInfo> list;
    auto activePids = GetActiveRingPids();
    for (uint32_t activePid : activePids) {
        ProcessInfo info;
        info.pid = activePid;
        info.name = GetProcessName(activePid);
        if (info.name.empty()) info.name = "Game";
        info.isActiveGame = true;
        list.push_back(info);
    }

    try {
        for (const auto& entry : std::filesystem::directory_iterator("/proc")) {
            if (!entry.is_directory()) continue;
            std::string filename = entry.path().filename().string();
            if (filename.empty() || !std::isdigit(filename[0])) continue;

            uint32_t pid = 0;
            try {
                pid = std::stoul(filename);
            } catch (...) { continue; }

            if (std::find(activePids.begin(), activePids.end(), pid) != activePids.end()) continue;

            std::string comm = GetProcessName(pid);
            if (!comm.empty()) {
                ProcessInfo info;
                info.pid = pid;
                info.name = comm;
                info.isActiveGame = false;
                list.push_back(info);
            }
        }
    } catch (...) {}

    return list;
}

namespace ProcUtils {
    using gnumon::common::ProcessInfo;
    using gnumon::common::GetTgidForPid;
    using gnumon::common::CleanStaleRings;
    using gnumon::common::GetActiveRingPids;
    using gnumon::common::GetProcessName;
    using gnumon::common::GetRunningProcesses;
}

} // namespace gnumon::common
