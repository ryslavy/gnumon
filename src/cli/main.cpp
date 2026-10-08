#include "../../include/gnumon/PresentMonAPI.h"
#include <iostream>
#include <iomanip>
#include <fstream>
#include <thread>
#include <chrono>
#include <vector>
#include <string>
#include <filesystem>
#include <algorithm>
#include <atomic>
#include <csignal>

namespace {
std::atomic<bool> g_running{true};
void SignalHandler(int) {
    g_running = false;
}

uint32_t FindPidByName(const std::string& processName) {
    for (const auto& entry : std::filesystem::directory_iterator("/proc")) {
        if (entry.is_directory()) {
            std::string filename = entry.path().filename().string();
            if (std::all_of(filename.begin(), filename.end(), ::isdigit)) {
                auto commPath = entry.path() / "comm";
                if (std::filesystem::exists(commPath)) {
                    std::ifstream f(commPath);
                    std::string comm;
                    f >> comm;
                    if (comm == processName) {
                        return static_cast<uint32_t>(std::stoul(filename));
                    }
                }
            }
        }
    }
    return 0;
}
} // namespace

#include <termios.h>
#include <unistd.h>
#include <fcntl.h>

struct TerminalRawMode {
    struct termios orig_termios{};
    bool active = false;

    TerminalRawMode() {
        if (isatty(STDIN_FILENO)) {
            tcgetattr(STDIN_FILENO, &orig_termios);
            struct termios raw = orig_termios;
            raw.c_lflag &= ~(ICANON | ECHO);
            raw.c_cc[VMIN] = 0;
            raw.c_cc[VTIME] = 0;
            tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
            active = true;
        }
    }

    ~TerminalRawMode() {
        if (active) {
            tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios);
        }
    }
};

bool CheckHotkeyPress(const std::string& hotkey) {
    char buf[16]{};
    int n = read(STDIN_FILENO, buf, sizeof(buf) - 1);
    if (n <= 0) return false;

    std::string s(buf);
    if ((hotkey == "F11" || hotkey == "f11") && s.find("\x1b[23~") != std::string::npos) return true;
    if ((hotkey == "F10" || hotkey == "f10") && s.find("\x1b[21~") != std::string::npos) return true;
    for (int i = 0; i < n; ++i) {
        if (hotkey == "space" && buf[i] == ' ') return true;
        if (buf[i] == 'r' || buf[i] == 'R') return true;
        if (buf[i] == '\n' || buf[i] == '\r') return true;
    }
    return false;
}

int main(int argc, char* argv[]) {
    std::signal(SIGINT, SignalHandler);
    std::signal(SIGTERM, SignalHandler);

    uint32_t targetPid = 0;
    std::string processName;
    std::string outputCsvPath;
    std::string hotkey;
    bool outputStdout = false;
    int timedSeconds = 0;
    int delaySeconds = 0;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if ((arg == "--process_id" || arg == "--pid") && i + 1 < argc) {
            targetPid = static_cast<uint32_t>(std::stoul(argv[++i]));
        } else if (arg == "--process_name" && i + 1 < argc) {
            processName = argv[++i];
        } else if ((arg == "--output_file" || arg == "-o") && i + 1 < argc) {
            outputCsvPath = argv[++i];
        } else if (arg == "--output_stdout") {
            outputStdout = true;
        } else if (arg == "--timed" && i + 1 < argc) {
            timedSeconds = std::stoi(argv[++i]);
        } else if (arg == "--delay" && i + 1 < argc) {
            delaySeconds = std::stoi(argv[++i]);
        } else if (arg == "--hotkey") {
            if (i + 1 < argc && argv[i + 1][0] != '-') {
                hotkey = argv[++i];
            } else {
                hotkey = "F11";
            }
        } else if (arg == "--install-layer") {
            const char* home = std::getenv("HOME");
            if (!home) {
                std::cerr << "HOME environment variable not set!" << std::endl;
                return 1;
            }
            std::filesystem::path homePath(home);
            std::filesystem::path implicitDir = homePath / ".local/share/vulkan/implicit_layer.d";
            std::filesystem::path explicitDir = homePath / ".local/share/vulkan/explicit_layer.d";
            std::filesystem::create_directories(implicitDir);
            std::filesystem::create_directories(explicitDir);

            std::filesystem::path exeDir = std::filesystem::canonical("/proc/self/exe").parent_path();
            std::filesystem::path layerLib = exeDir / "libVkLayer_gnumon.so";
            if (!std::filesystem::exists(layerLib)) {
                layerLib = homePath / ".local/lib/gnumon/libVkLayer_gnumon.so";
            }
            if (!std::filesystem::exists(layerLib)) {
                layerLib = "/usr/local/lib/libVkLayer_gnumon.so";
            }
            if (!std::filesystem::exists(layerLib)) {
                layerLib = "/usr/lib/libVkLayer_gnumon.so";
            }

            std::ofstream imp(implicitDir / "VkLayer_gnumon.json");
            imp << "{\n"
                << "    \"file_format_version\" : \"1.0.0\",\n"
                << "    \"layer\" : {\n"
                << "        \"name\": \"VK_LAYER_GNUMON_capture\",\n"
                << "        \"type\": \"GLOBAL\",\n"
                << "        \"library_path\": \"" << layerLib.string() << "\",\n"
                << "        \"api_version\": \"1.3.0\",\n"
                << "        \"implementation_version\": \"1\",\n"
                << "        \"description\": \"gnumon Linux PresentMon frame capture layer\",\n"
                << "        \"functions\": {\n"
                << "            \"vkNegotiateLoaderLayerInterfaceVersion\": \"vkNegotiateLoaderLayerInterfaceVersion\"\n"
                << "        },\n"
                << "        \"enable_environment\": {\n"
                << "            \"ENABLE_GNUMON\": \"1\"\n"
                << "        },\n"
                << "        \"disable_environment\": {\n"
                << "            \"DISABLE_GNUMON\": \"1\"\n"
                << "        }\n"
                << "    }\n"
                << "}\n";

            std::ofstream exp(explicitDir / "VkLayer_gnumon.json");
            exp << "{\n"
                << "    \"file_format_version\" : \"1.0.0\",\n"
                << "    \"layer\" : {\n"
                << "        \"name\": \"VK_LAYER_GNUMON_capture\",\n"
                << "        \"type\": \"GLOBAL\",\n"
                << "        \"library_path\": \"" << layerLib.string() << "\",\n"
                << "        \"api_version\": \"1.3.0\",\n"
                << "        \"implementation_version\": \"1\",\n"
                << "        \"description\": \"gnumon Linux PresentMon frame capture layer\",\n"
                << "        \"functions\": {\n"
                << "            \"vkNegotiateLoaderLayerInterfaceVersion\": \"vkNegotiateLoaderLayerInterfaceVersion\"\n"
                << "        }\n"
                << "    }\n"
                << "}\n";

            std::cout << "[gnumon] Successfully installed Vulkan layers to " << homePath / ".local/share/vulkan" << "\n"
                      << "[gnumon] Pointing to library: " << layerLib.string() << "\n"
                      << "[gnumon] Usage in Steam/games: ENABLE_GNUMON=1 %command%\n";
            return 0;
        } else if (arg == "--uninstall-layer") {
            const char* home = std::getenv("HOME");
            if (!home) return 1;
            std::filesystem::path homePath(home);
            std::filesystem::remove(homePath / ".local/share/vulkan/implicit_layer.d/VkLayer_gnumon.json");
            std::filesystem::remove(homePath / ".local/share/vulkan/explicit_layer.d/VkLayer_gnumon.json");
            std::cout << "[gnumon] Successfully uninstalled Vulkan layers from user profile.\n";
            return 0;
        } else if (arg == "--help" || arg == "-h") {
            std::cout << "Usage: gnumon-cli [options] [PID]\n"
                      << "Options:\n"
                      << "  --process_id, --pid <PID>   Record target process by ID\n"
                      << "  --process_name <name>       Record target process by executable name\n"
                      << "  --output_file, -o <path>    Write CSV output to file\n"
                      << "  --output_stdout             Write CSV output to standard output\n"
                      << "  --hotkey [key]              Toggle recording with hotkey (default F11 or 'r')\n"
                      << "  --delay <seconds>           Delay before starting capture\n"
                      << "  --timed <seconds>           Stop recording after specified seconds\n"
                      << "  --install-layer             Register Vulkan layer into ~/.local/share/vulkan\n"
                      << "  --uninstall-layer           Unregister Vulkan layer from user profile\n"
                      << "  --help, -h                  Show this help message\n";
            return 0;
        } else if (arg.rfind("-", 0) != 0) {
            try {
                targetPid = static_cast<uint32_t>(std::stoul(arg));
            } catch (...) {}
        }
    }

    if (!processName.empty() && targetPid == 0) {
        targetPid = FindPidByName(processName);
        if (targetPid == 0) {
            std::cerr << "Process '" << processName << "' not found!" << std::endl;
            return 1;
        }
    }

    if (!outputStdout) {
        std::cout << "========================================" << std::endl;
        std::cout << "    gnumon (PresentMon for Linux) CLI   " << std::endl;
        std::cout << "========================================" << std::endl;

        PM_VERSION ver{};
        if (pmGetApiVersion(&ver) == PM_STATUS_SUCCESS) {
            std::cout << "API Version: " << ver.major << "." << ver.minor << "." << ver.patch
                      << " (" << ver.tag << ")" << std::endl;
        }

        if (targetPid > 0) {
            std::cout << "Target PID: " << targetPid << (processName.empty() ? "" : (" (" + processName + ")")) << std::endl;
        } else {
            std::cout << "No target PID (system-wide hardware monitoring only)." << std::endl;
        }
    }

    if (delaySeconds > 0) {
        if (!outputStdout) std::cout << "Waiting " << delaySeconds << "s before capture..." << std::endl;
        std::this_thread::sleep_for(std::chrono::seconds(delaySeconds));
    }

    PM_SESSION_HANDLE session = nullptr;
    if (pmOpenSession(&session) != PM_STATUS_SUCCESS) {
        std::cerr << "Failed to open gnumon session!" << std::endl;
        return 1;
    }

    if (targetPid > 0) {
        pmStartTrackingProcess(session, targetPid);
    }

    struct alignas(8) FrameQueryRecord {
        uint32_t processId;
        uint64_t swapChain;
        int32_t runtime;
        int32_t presentMode;
        int32_t frameType;
        double cpuStartTimeMs;
        uint64_t cpuStartQpc;
        double frameTimeMs;
        double fps;
        double inPresentApiMs;
        double presentStartTimeSec;
        uint64_t presentStartQpc;
        double gpuTimeMs;
        double gpuPower;
        double gpuTemp;
        double gpuUtil;
        double gpuFreq;
        double cpuUtil;
        double cpuPower;
        double cpuTemp;
    };

    std::vector<PM_QUERY_ELEMENT> elements = {
        { PM_METRIC_PROCESS_ID, PM_STAT_NONE, 0, 0, offsetof(FrameQueryRecord, processId), sizeof(uint32_t) },
        { PM_METRIC_SWAP_CHAIN_ADDRESS, PM_STAT_NONE, 0, 0, offsetof(FrameQueryRecord, swapChain), sizeof(uint64_t) },
        { PM_METRIC_PRESENT_RUNTIME, PM_STAT_NONE, 0, 0, offsetof(FrameQueryRecord, runtime), sizeof(int32_t) },
        { PM_METRIC_PRESENT_MODE, PM_STAT_NONE, 0, 0, offsetof(FrameQueryRecord, presentMode), sizeof(int32_t) },
        { PM_METRIC_FRAME_TYPE, PM_STAT_NONE, 0, 0, offsetof(FrameQueryRecord, frameType), sizeof(int32_t) },
        { PM_METRIC_CPU_START_TIME, PM_STAT_NONE, 0, 0, offsetof(FrameQueryRecord, cpuStartTimeMs), sizeof(double) },
        { PM_METRIC_CPU_START_QPC, PM_STAT_NONE, 0, 0, offsetof(FrameQueryRecord, cpuStartQpc), sizeof(uint64_t) },
        { PM_METRIC_CPU_FRAME_TIME, PM_STAT_NONE, 0, 0, offsetof(FrameQueryRecord, frameTimeMs), sizeof(double) },
        { PM_METRIC_DISPLAYED_FPS, PM_STAT_NONE, 0, 0, offsetof(FrameQueryRecord, fps), sizeof(double) },
        { PM_METRIC_IN_PRESENT_API, PM_STAT_NONE, 0, 0, offsetof(FrameQueryRecord, inPresentApiMs), sizeof(double) },
        { PM_METRIC_PRESENT_START_TIME, PM_STAT_NONE, 0, 0, offsetof(FrameQueryRecord, presentStartTimeSec), sizeof(double) },
        { PM_METRIC_PRESENT_START_QPC, PM_STAT_NONE, 0, 0, offsetof(FrameQueryRecord, presentStartQpc), sizeof(uint64_t) },
        { PM_METRIC_GPU_TIME, PM_STAT_NONE, 0, 0, offsetof(FrameQueryRecord, gpuTimeMs), sizeof(double) },
        { PM_METRIC_GPU_POWER, PM_STAT_NONE, 0, 0, offsetof(FrameQueryRecord, gpuPower), sizeof(double) },
        { PM_METRIC_GPU_TEMPERATURE, PM_STAT_NONE, 0, 0, offsetof(FrameQueryRecord, gpuTemp), sizeof(double) },
        { PM_METRIC_GPU_UTILIZATION, PM_STAT_NONE, 0, 0, offsetof(FrameQueryRecord, gpuUtil), sizeof(double) },
        { PM_METRIC_GPU_FREQUENCY, PM_STAT_NONE, 0, 0, offsetof(FrameQueryRecord, gpuFreq), sizeof(double) },
        { PM_METRIC_CPU_UTILIZATION, PM_STAT_NONE, 0, 0, offsetof(FrameQueryRecord, cpuUtil), sizeof(double) },
        { PM_METRIC_CPU_POWER, PM_STAT_NONE, 0, 0, offsetof(FrameQueryRecord, cpuPower), sizeof(double) },
        { PM_METRIC_CPU_TEMPERATURE, PM_STAT_NONE, 0, 0, offsetof(FrameQueryRecord, cpuTemp), sizeof(double) },
    };

    PM_FRAME_QUERY_HANDLE frameQuery = nullptr;
    uint32_t blobSize = 0;
    if (pmRegisterFrameQuery(session, &frameQuery, elements.data(), elements.size(), &blobSize) != PM_STATUS_SUCCESS) {
        std::cerr << "Failed to register frame query!" << std::endl;
        pmCloseSession(session);
        return 1;
    }

    std::ofstream csvFile;
    if (!outputCsvPath.empty()) {
        csvFile.open(outputCsvPath);
        if (!csvFile.is_open()) {
            std::cerr << "Could not open output CSV file: " << outputCsvPath << std::endl;
        }
    }

    const std::string csvHeader =
        "Application,ProcessID,SwapChainAddress,Runtime,PresentMode,FrameType,"
        "CPUStartTimeMs,CPUStartQPC,FrameTimeMs,FPS,MsInPresentAPI,PresentStartTimeSec,PresentStartQPC,"
        "GPUTimeMs,GPUPowerW,GPUTempC,GPUUtilPercent,GPUFreqMHz,CPUUtilPercent,CPUPowerW,CPUTempC";

    if (csvFile.is_open()) {
        csvFile << csvHeader << "\n";
    }
    if (outputStdout) {
        std::cout << csvHeader << "\n";
    }

    if (!outputStdout && outputCsvPath.empty()) {
        std::cout << "\nPolling live telemetry (Ctrl+C to stop):\n" << std::endl;
        std::cout << std::left
                  << std::setw(10) << "FPS"
                  << std::setw(14) << "FrameTime(ms)"
                  << std::setw(14) << "Present(ms)"
                  << std::setw(14) << "GPUTime(ms)"
                  << std::setw(12) << "GPU Pow(W)"
                  << std::setw(12) << "GPU Temp(C)"
                  << std::setw(12) << "GPU Util(%)"
                  << std::setw(14) << "GPU Freq(MHz)"
                  << std::setw(12) << "CPU Util(%)"
                  << std::setw(12) << "CPU Pow(W)"
                  << std::setw(12) << "CPU Temp(C)"
                  << std::endl;
        std::cout << std::string(132, '-') << std::endl;
    }

    std::unique_ptr<TerminalRawMode> rawMode;
    bool recordingActive = outputStdout || !outputCsvPath.empty();
    if (!hotkey.empty()) {
        rawMode = std::make_unique<TerminalRawMode>();
        recordingActive = false; // Wait for hotkey press
        std::cout << "[Hotkey enabled: Press '" << hotkey << "' to start/stop recording]\n" << std::endl;
    }

    auto startTime = std::chrono::steady_clock::now();
    std::vector<uint8_t> buffer(blobSize * 128);
    uint64_t totalFramesCaptured = 0;

    while (g_running) {
        bool hotkeyHit = false;
        if (!hotkey.empty()) {
            hotkeyHit = CheckHotkeyPress(hotkey);
        }
        bool globalTrigger = false;
        if (pmCheckHotkeyTriggered(session, &globalTrigger) == PM_STATUS_SUCCESS && globalTrigger) {
            hotkeyHit = true;
        }

        if (hotkeyHit) {
            recordingActive = !recordingActive;
                if (recordingActive) {
                    if (!csvFile.is_open()) {
                        std::string path = outputCsvPath.empty()
                            ? ("gnumon_capture_" + std::to_string(std::chrono::system_clock::now().time_since_epoch().count()) + ".csv")
                            : outputCsvPath;
                        csvFile.open(path);
                        if (csvFile.is_open()) csvFile << csvHeader << "\n";
                        std::cout << "\n>>> [HOTKEY] Recording STARTED -> " << path << std::endl;
                    }
                } else {
                    if (csvFile.is_open()) {
                        csvFile.flush();
                        csvFile.close();
                        std::cout << "\n>>> [HOTKEY] Recording STOPPED (saved " << totalFramesCaptured << " frames)\n" << std::endl;
                    }
                }
            }

        uint32_t framesToRead = 128;
        if (pmConsumeFrames(frameQuery, targetPid, buffer.data(), &framesToRead) == PM_STATUS_SUCCESS && framesToRead > 0) {
            for (uint32_t i = 0; i < framesToRead; ++i) {
                const auto* rec = reinterpret_cast<const FrameQueryRecord*>(buffer.data() + (i * blobSize));
                if (recordingActive) {
                    totalFramesCaptured++;
                }

                if (recordingActive && (csvFile.is_open() || outputStdout)) {
                    std::ostringstream ss;
                    ss << (processName.empty() ? "App" : processName) << ","
                       << rec->processId << ","
                       << "0x" << std::hex << rec->swapChain << std::dec << ","
                       << "Vulkan" << ","
                       << "Composed_Flip" << ","
                       << "Application" << ","
                       << std::fixed << std::setprecision(3)
                       << rec->cpuStartTimeMs << ","
                       << rec->cpuStartQpc << ","
                       << rec->frameTimeMs << ","
                       << std::setprecision(1) << rec->fps << ","
                       << std::setprecision(3) << rec->inPresentApiMs << ","
                       << std::setprecision(6) << rec->presentStartTimeSec << ","
                       << rec->presentStartQpc << ","
                       << std::setprecision(3) << rec->gpuTimeMs << ","
                       << std::setprecision(1)
                       << rec->gpuPower << ","
                       << rec->gpuTemp << ","
                       << rec->gpuUtil << ","
                       << rec->gpuFreq << ","
                       << rec->cpuUtil << ","
                       << rec->cpuPower << ","
                       << rec->cpuTemp << "\n";

                    if (csvFile.is_open()) csvFile << ss.str();
                    if (outputStdout) std::cout << ss.str();
                } else {
                    // Live console output for latest frame in batch
                    if (i == framesToRead - 1) {
                        std::cout << std::fixed << std::setprecision(1)
                                  << std::setw(10) << rec->fps
                                  << std::setw(14) << rec->frameTimeMs
                                  << std::setw(14) << rec->inPresentApiMs
                                  << std::setw(12) << rec->gpuPower
                                  << std::setw(12) << rec->gpuTemp
                                  << std::setw(12) << rec->gpuUtil
                                  << std::setw(14) << rec->gpuFreq
                                  << std::setw(12) << rec->cpuUtil
                                  << std::setw(12) << rec->cpuPower
                                  << std::setw(12) << rec->cpuTemp
                                  << std::endl;
                    }
                }
            }
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(10));

        if (timedSeconds > 0) {
            auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::steady_clock::now() - startTime).count();
            if (elapsed >= timedSeconds) {
                break;
            }
        }
    }

    if (csvFile.is_open()) {
        csvFile.flush();
        csvFile.close();
        if (!outputStdout) {
            std::cout << "\nSaved " << totalFramesCaptured << " frames to " << outputCsvPath << std::endl;
        }
    }

    pmFreeFrameQuery(frameQuery);
    if (targetPid > 0) {
        pmStopTrackingProcess(session, targetPid);
    }
    pmCloseSession(session);

    if (!outputStdout) {
        std::cout << "Done." << std::endl;
    }
    return 0;
}
