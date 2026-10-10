#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>

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
                if (pid > 0 && !std::filesystem::exists("/proc/" + std::to_string(pid))) {
                    shm_unlink(("/" + name).c_str());
                }
            } catch (...) {}
        }
    }
}

// Scans /dev/shm for live running processes with an active gnumon ring buffer
inline std::vector<uint32_t> GetActiveRingPids() {
    CleanStaleRings();
    std::vector<uint32_t> activePids;
    std::filesystem::path shmDir("/dev/shm");
    if (!std::filesystem::exists(shmDir)) return activePids;

    for (const auto& entry : std::filesystem::directory_iterator(shmDir)) {
        std::string name = entry.path().filename().string();
        if (name.rfind("gnumon_ring_", 0) == 0) {
            std::string pidStr = name.substr(12);
            try {
                uint32_t pid = std::stoul(pidStr);
                if (pid > 0 && std::filesystem::exists("/proc/" + std::to_string(pid))) {
                    activePids.push_back(pid);
                }
            } catch (...) {}
        }
    }
    return activePids;
}

inline std::string GetProcessName(uint32_t pid) {
    if (pid == 0) return "";
    std::string commPath = "/proc/" + std::to_string(pid) + "/comm";
    std::ifstream commFile(commPath);
    if (commFile.is_open()) {
        std::string comm;
        std::getline(commFile, comm);
        return comm;
    }
    return "";
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
