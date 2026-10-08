#include "../service/TelemetryCoordinator.h"
#include "../common/Clock.h"
#include <iostream>
#include <string>
#include <vector>
#include <thread>
#include <chrono>
#include <atomic>
#include <csignal>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>
#include <sstream>

namespace {
std::atomic<bool> g_running{true};

void SignalHandler(int signum) {
    (void)signum;
    g_running = false;
}

std::string GetSocketPath() {
    const char* xdgRuntime = std::getenv("XDG_RUNTIME_DIR");
    if (xdgRuntime && std::string(xdgRuntime).length() > 0) {
        return std::string(xdgRuntime) + "/gnumon.sock";
    }
    return "/tmp/gnumon.sock";
}
} // namespace

int main(int argc, char* argv[]) {
    bool foreground = false;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "-f" || arg == "--foreground") {
            foreground = true;
        } else if (arg == "-h" || arg == "--help") {
            std::cout << "Usage: gnumond [OPTIONS]\n"
                      << "Linux PresentMon background service daemon\n\n"
                      << "Options:\n"
                      << "  -f, --foreground    Run in foreground (do not daemonize)\n"
                      << "  -h, --help          Show this help message\n";
            return 0;
        }
    }

    if (!foreground) {
        if (daemon(0, 0) < 0) {
            std::cerr << "Failed to daemonize process" << std::endl;
            return 1;
        }
    }

    std::signal(SIGINT, SignalHandler);
    std::signal(SIGTERM, SignalHandler);
    std::signal(SIGPIPE, SIG_IGN);

    std::string socketPath = GetSocketPath();
    unlink(socketPath.c_str());

    int serverFd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (serverFd < 0) {
        std::cerr << "Failed to create UNIX domain socket" << std::endl;
        return 1;
    }

    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    std::strncpy(addr.sun_path, socketPath.c_str(), sizeof(addr.sun_path) - 1);

    if (bind(serverFd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        std::cerr << "Failed to bind to " << socketPath << std::endl;
        close(serverFd);
        return 1;
    }

    if (listen(serverFd, 16) < 0) {
        std::cerr << "Failed to listen on socket" << std::endl;
        close(serverFd);
        return 1;
    }

    // Set non-blocking
    int flags = fcntl(serverFd, F_GETFL, 0);
    fcntl(serverFd, F_SETFL, flags | O_NONBLOCK);

    std::cout << "========================================" << std::endl;
    std::cout << "   gnumond — PresentMon Linux Daemon    " << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << "Listening on: " << socketPath << std::endl;

    gnumon::service::TelemetryCoordinator coordinator;
    coordinator.Initialize();

    std::vector<pollfd> pollFds;
    pollFds.push_back({ serverFd, POLLIN, 0 });

    auto lastSampleTime = std::chrono::steady_clock::now();

    while (g_running) {
        // Poll every 50ms
        int ret = poll(pollFds.data(), pollFds.size(), 50);

        auto now = std::chrono::steady_clock::now();
        if (std::chrono::duration_cast<std::chrono::milliseconds>(now - lastSampleTime).count() >= 100) {
            coordinator.SampleAll();
            lastSampleTime = now;
        }

        if (ret > 0) {
            // Check for new client connections
            if (pollFds[0].revents & POLLIN) {
                int clientFd = accept(serverFd, nullptr, nullptr);
                if (clientFd >= 0) {
                    int cflags = fcntl(clientFd, F_GETFL, 0);
                    fcntl(clientFd, F_SETFL, cflags | O_NONBLOCK);
                    pollFds.push_back({ clientFd, POLLIN, 0 });
                }
            }

            // Check client requests
            for (size_t i = 1; i < pollFds.size();) {
                if (pollFds[i].revents & (POLLERR | POLLHUP)) {
                    close(pollFds[i].fd);
                    pollFds.erase(pollFds.begin() + i);
                    continue;
                }

                if (pollFds[i].revents & POLLIN) {
                    char buf[1024]{};
                    ssize_t n = read(pollFds[i].fd, buf, sizeof(buf) - 1);
                    if (n <= 0) {
                        close(pollFds[i].fd);
                        pollFds.erase(pollFds.begin() + i);
                        continue;
                    }

                    std::string cmd(buf);
                    // Trim newline
                    while (!cmd.empty() && (cmd.back() == '\n' || cmd.back() == '\r')) {
                        cmd.pop_back();
                    }

                    std::ostringstream resp;
                    if (cmd == "PING") {
                        resp << "PONG\n";
                    } else if (cmd.rfind("TRACK ", 0) == 0) {
                        uint32_t pid = std::stoul(cmd.substr(6));
                        bool ok = coordinator.StartTrackingProcess(pid);
                        resp << (ok ? "OK" : "ERROR") << "\n";
                    } else if (cmd.rfind("UNTRACK ", 0) == 0) {
                        uint32_t pid = std::stoul(cmd.substr(8));
                        coordinator.StopTrackingProcess(pid);
                        resp << "OK\n";
                    } else if (cmd == "SAMPLE") {
                        auto gpu = coordinator.GetLatestGpuMetrics();
                        auto cpu = coordinator.GetLatestCpuMetrics();
                        gnumon::ipc::FrameEvent frame{};
                        coordinator.GetLatestFrame(frame);

                        double fps = (frame.frameTimeNs > 0) ? (1'000'000'000.0 / static_cast<double>(frame.frameTimeNs)) : 0.0;
                        double ftMs = static_cast<double>(frame.frameTimeNs) / 1'000'000.0;
                        double gpuTimeMs = static_cast<double>(frame.gpuDurationNs) / 1'000'000.0;

                        resp << "{"
                             << "\"fps\":" << fps << ","
                             << "\"frametime_ms\":" << ftMs << ","
                             << "\"gpu_time_ms\":" << gpuTimeMs << ","
                             << "\"gpu_power_w\":" << gpu.powerWatts << ","
                             << "\"gpu_temp_c\":" << gpu.temperatureEdgeC << ","
                             << "\"gpu_util_percent\":" << gpu.gpuUtilizationPercent << ","
                             << "\"gpu_freq_mhz\":" << gpu.gpuFrequencyMhz << ","
                             << "\"cpu_power_w\":" << cpu.cpuPackagePowerWatts << ","
                             << "\"cpu_temp_c\":" << cpu.cpuTemperatureC << ","
                             << "\"cpu_util_percent\":" << cpu.cpuUtilizationPercent
                             << "}\n";
                    } else {
                        resp << "UNKNOWN_COMMAND\n";
                    }

                    std::string out = resp.str();
                    [[maybe_unused]] auto written = write(pollFds[i].fd, out.data(), out.size());
                }
                ++i;
            }
        }
    }

    std::cout << "\nShutting down gnumond..." << std::endl;
    for (const auto& p : pollFds) {
        close(p.fd);
    }
    unlink(socketPath.c_str());
    return 0;
}
