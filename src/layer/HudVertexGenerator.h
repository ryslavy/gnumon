#pragma once

#include <vector>
#include <string>
#include <cstring>
#include <cmath>
#include <cstdio>
#include <algorithm>
#include <deque>
#include <cstdint>
#include <fstream>
#include <sys/stat.h>
#include "font_atlas.hpp"
#include "../ipc/FrameRingBuffer.h"
#include "../common/Clock.h"
#include <gnumon/PresentMonAPI.h>

namespace gnumon::layer {

struct OverlayVertex {
    float x, y;
    float u, v;
    float r, g, b, a;
};

struct OverlayMetricLine {
    int metricId = 12;
    int statId = 1; // 1: avg, 6: 99%, 4: raw, 5: 1%, 2: min, 3: max, 0: none
    float r = 0.0f, g = 0.90f, b = 1.0f, a = 1.0f;
    float fillR = 0.0f, fillG = 0.85f, fillB = 1.0f, fillA = 0.20f;
    std::string label = "Metric";
    std::string units = "";
};

struct OverlayWidgetSpec {
    bool isGraph = false;
    bool isHistogram = false;
    float rangeMin = 0.0f;
    float rangeMax = 50.0f;
    bool autoScale = true;
    std::vector<OverlayMetricLine> lines;
};

class HudVertexGenerator {
public:
    HudVertexGenerator() {
        InitDefaultWidgets();
    }
    virtual ~HudVertexGenerator() = default;

    void SetPreset(int preset) {
        hudPreset_ = std::clamp(preset, 0, 2);
    }

    int GetPreset() const {
        return hudPreset_;
    }

    void AddFrametimeSample(float ms) {
        PushSample(frametimes_, ms);
    }

    void TriggerToast(const std::string& title, const std::string& msg, float durationSec = 2.5f) {
        toastTitle_ = title;
        toastMessage_ = msg;
        toastExpiryNs_ = gnumon::common::Clock::GetTimestampNs() + static_cast<uint64_t>(durationSec * 1'000'000'000.0f);
    }

    bool HasActiveToast() const {
        return gnumon::common::Clock::GetTimestampNs() < toastExpiryNs_ && !toastTitle_.empty();
    }

    static void AddQuad(std::vector<OverlayVertex>& verts, float x, float y, float w, float h,
                        float r, float g, float b, float a) {
        OverlayVertex v00{x, y, -1.0f, -1.0f, r, g, b, a};
        OverlayVertex v10{x + w, y, -1.0f, -1.0f, r, g, b, a};
        OverlayVertex v11{x + w, y + h, -1.0f, -1.0f, r, g, b, a};
        OverlayVertex v01{x, y + h, -1.0f, -1.0f, r, g, b, a};

        verts.push_back(v00);
        verts.push_back(v10);
        verts.push_back(v11);
        verts.push_back(v00);
        verts.push_back(v11);
        verts.push_back(v01);
    }

    static void AddLine(std::vector<OverlayVertex>& verts, float x0, float y0, float x1, float y1,
                        float thickness, float r, float g, float b, float a) {
        float dx = x1 - x0;
        float dy = y1 - y0;
        float len = std::sqrt(dx * dx + dy * dy);
        if (len < 0.0001f) return;

        float nx = (-dy / len) * (thickness * 0.5f);
        float ny = (dx / len) * (thickness * 0.5f);

        OverlayVertex v0{x0 - nx, y0 - ny, -1.0f, -1.0f, r, g, b, a};
        OverlayVertex v1{x1 - nx, y1 - ny, -1.0f, -1.0f, r, g, b, a};
        OverlayVertex v2{x1 + nx, y1 + ny, -1.0f, -1.0f, r, g, b, a};
        OverlayVertex v3{x0 + nx, y0 + ny, -1.0f, -1.0f, r, g, b, a};

        verts.push_back(v0);
        verts.push_back(v1);
        verts.push_back(v2);
        verts.push_back(v0);
        verts.push_back(v2);
        verts.push_back(v3);
    }

    static float AddChar(std::vector<OverlayVertex>& verts, char c, float x, float y, float scale,
                         float r, float g, float b, float a) {
        if (c < 32 || c > 126) c = ' ';
        uint8_t c_idx = static_cast<uint8_t>(c - 32);
        float cw = static_cast<float>(font_glyph_advances[c_idx]) * scale;
        float gw = static_cast<float>(FONT_GLYPH_W) * scale;
        float gh = static_cast<float>(FONT_GLYPH_H) * scale;

        float u0 = static_cast<float>(c_idx * FONT_GLYPH_W) / static_cast<float>(FONT_TEX_W);
        float u1 = u0 + static_cast<float>(FONT_GLYPH_W) / static_cast<float>(FONT_TEX_W);
        float v0 = 0.0f;
        float v1 = 1.0f;

        OverlayVertex v00{x, y, u0, v0, r, g, b, a};
        OverlayVertex v10{x + gw, y, u1, v0, r, g, b, a};
        OverlayVertex v11{x + gw, y + gh, u1, v1, r, g, b, a};
        OverlayVertex v01{x, y + gh, u0, v1, r, g, b, a};

        verts.push_back(v00);
        verts.push_back(v10);
        verts.push_back(v11);
        verts.push_back(v00);
        verts.push_back(v11);
        verts.push_back(v01);

        return cw;
    }

    static float AddString(std::vector<OverlayVertex>& verts, const std::string& str,
                           float x, float y, float scale, float r, float g, float b, float a) {
        float cur_x = x;
        for (char c : str) {
            float cw = AddChar(verts, c, cur_x, y, scale, r, g, b, a);
            cur_x += cw;
        }
        return cur_x - x;
    }

    void PushSample(std::deque<float>& q, float val, size_t maxLen = 128) {
        q.push_back(val);
        if (q.size() > maxLen) {
            q.pop_front();
        }
    }

    void RecordMetrics(double presentFps, double displayedFps, double frameTimeMs, double latencyMs, double animErrorMs,
                       const ipc::TelemetrySnapshot* telem = nullptr) {
        if (frameTimeMs > 0.0) {
            PushSample(frametimes_, static_cast<float>(frameTimeMs));
        }
        PushSample(histUntilDisplayed_, static_cast<float>(latencyMs));
        PushSample(histFps_, static_cast<float>(displayedFps > 0.0 ? displayedFps : presentFps));
        PushSample(histAnimError_, static_cast<float>(animErrorMs));

        // Dropped frame estimation: frame longer than 1.5 * standard vsync (16.6ms)
        float dropped = (frameTimeMs > 25.0) ? 1.0f : 0.0f;
        PushSample(histDroppedFrames_, dropped);

        if (telem && telem->valid != 0) {
            PushSample(histGpuUtil_, telem->gpuUtil);
            PushSample(histGpuPower_, telem->gpuPower);
            PushSample(histGpuTemp_, telem->gpuTemp);
            PushSample(histGpuFreq_, telem->gpuFreq);
            PushSample(histVramUsed_, telem->vramUsedGb);
            PushSample(histCpuUtil_, telem->cpuUtil);
            PushSample(histCpuPower_, telem->cpuPower);
            PushSample(histCpuTemp_, telem->cpuTemp);
            PushSample(histCpuFreq_, telem->cpuFreq);
        }
    }

    const std::deque<float>& GetHistoryForMetric(int metricId) const {
        switch (metricId) {
            case PM_METRIC_BETWEEN_DISPLAY_CHANGE:
            case PM_METRIC_DISPLAYED_FRAME_TIME:
            case PM_METRIC_PRESENTED_FRAME_TIME:
            case PM_METRIC_BETWEEN_PRESENTS:
            case PM_METRIC_BETWEEN_APP_START:
            case PM_METRIC_BETWEEN_SIMULATION_START:
            case PM_METRIC_CPU_FRAME_TIME:
            case PM_METRIC_IN_PRESENT_API:
            case PM_METRIC_FLIP_DELAY:
                return frametimes_;
            case PM_METRIC_UNTIL_DISPLAYED:
            case PM_METRIC_DISPLAY_LATENCY:
            case PM_METRIC_CLICK_TO_PHOTON_LATENCY:
            case PM_METRIC_ALL_INPUT_TO_PHOTON_LATENCY:
            case PM_METRIC_INSTRUMENTED_LATENCY:
            case PM_METRIC_PC_LATENCY:
            case PM_METRIC_GPU_LATENCY:
            case PM_METRIC_RENDER_PRESENT_LATENCY:
                return histUntilDisplayed_;
            case PM_METRIC_DROPPED_FRAMES:
                return histDroppedFrames_;
            case PM_METRIC_APPLICATION_FPS:
            case PM_METRIC_DISPLAYED_FPS:
            case PM_METRIC_PRESENTED_FPS:
                return histFps_;
            case PM_METRIC_ANIMATION_ERROR:
            case PM_METRIC_ANIMATION_TIME:
                return histAnimError_;
            case PM_METRIC_GPU_UTILIZATION:
            case PM_METRIC_GPU_RENDER_COMPUTE_UTILIZATION:
            case PM_METRIC_GPU_MEDIA_UTILIZATION:
                return histGpuUtil_;
            case PM_METRIC_GPU_POWER:
            case PM_METRIC_GPU_CARD_POWER:
            case PM_METRIC_GPU_SUSTAINED_POWER_LIMIT:
                return histGpuPower_;
            case PM_METRIC_GPU_TEMPERATURE:
            case PM_METRIC_GPU_VOLTAGE_REGULATOR_TEMPERATURE:
                return histGpuTemp_;
            case PM_METRIC_GPU_FREQUENCY:
            case PM_METRIC_GPU_EFFECTIVE_FREQUENCY:
                return histGpuFreq_;
            case PM_METRIC_GPU_MEM_USED:
            case PM_METRIC_GPU_MEM_SIZE:
            case PM_METRIC_GPU_MEM_UTILIZATION:
                return histVramUsed_;
            case PM_METRIC_CPU_UTILIZATION:
            case PM_METRIC_CPU_CORE_UTILITY:
                return histCpuUtil_;
            case PM_METRIC_CPU_POWER:
            case PM_METRIC_CPU_POWER_LIMIT:
                return histCpuPower_;
            case PM_METRIC_CPU_TEMPERATURE:
            case PM_METRIC_CPU_CORE_TEMPERATURE:
                return histCpuTemp_;
            case PM_METRIC_CPU_FREQUENCY:
                return histCpuFreq_;
            default:
                return frametimes_;
        }
    }

    static float CalculateStat(const std::deque<float>& q, int statId) {
        if (q.empty()) return 0.0f;
        if (statId == 4 || statId == 0) { // Raw / None
            return q.back();
        }
        if (statId == 1) { // Avg
            double sum = 0.0;
            for (float v : q) sum += v;
            return static_cast<float>(sum / q.size());
        }
        if (statId == 2) { // Min
            float m = q.front();
            for (float v : q) if (v < m) m = v;
            return m;
        }
        if (statId == 3) { // Max
            float m = q.front();
            for (float v : q) if (v > m) m = v;
            return m;
        }
        // Percentile stats: 5 (1% Low), 6 (99% / 99% Low)
        std::vector<float> sorted(q.begin(), q.end());
        std::sort(sorted.begin(), sorted.end());
        if (statId == 5) {
            size_t idx = static_cast<size_t>(std::floor(sorted.size() * 0.01));
            return sorted[std::min(idx, sorted.size() - 1)];
        }
        if (statId == 6) {
            size_t idx = static_cast<size_t>(std::floor(sorted.size() * 0.99));
            return sorted[std::min(idx, sorted.size() - 1)];
        }
        return q.back();
    }

    void CheckReloadConfig(uint64_t nowNs) {
        if (nowNs < lastConfigCheckNs_ + 500'000'000) {
            return; // Check at most every 500 ms
        }
        lastConfigCheckNs_ = nowNs;

        const char* home = getenv("HOME");
        if (!home) return;
        std::string cfgPath = std::string(home) + "/.config/gnumon/config.ini";

        struct stat st{};
        if (stat(cfgPath.c_str(), &st) != 0) return;
        if (st.st_mtime == lastConfigMtime_) return;
        lastConfigMtime_ = st.st_mtime;

        // Parse config.ini
        std::ifstream file(cfgPath);
        if (!file.is_open()) return;

        std::string line;
        std::string currentGroup;
        std::vector<OverlayWidgetSpec> loadedWidgets;
        int widgetCount = 0;

        while (std::getline(file, line)) {
            // Trim leading whitespace
            size_t first = line.find_first_not_of(" \t\r\n");
            if (first == std::string::npos || line[first] == '#' || line[first] == ';') continue;
            line = line.substr(first);

            if (line.front() == '[' && line.back() == ']') {
                currentGroup = line.substr(1, line.size() - 2);
                continue;
            }

            auto eq = line.find('=');
            if (eq == std::string::npos) continue;
            std::string key = line.substr(0, eq);
            std::string val = line.substr(eq + 1);
            // Trim whitespace
            key.erase(key.find_last_not_of(" \t\r\n") + 1);
            val.erase(0, val.find_first_not_of(" \t\r\n"));
            val.erase(val.find_last_not_of(" \t\r\n") + 1);

            if (currentGroup == "Hotkeys") {
                if (key == "overlay") hotkeyOverlay_ = val;
                else if (key == "presetCycle") hotkeyPresetCycle_ = val;
                else if (key == "capture") hotkeyCapture_ = val;
            } else if (currentGroup == "Overlay") {
                if (key == "inGameHudCorner") {
                    try { configuredCorner_ = std::stoi(val); } catch (...) {}
                } else if (key == "width") {
                    try { configuredWidth_ = std::stof(val); } catch (...) {}
                }
            } else if (currentGroup == "Loadout") {
                if (key == "widgetCount") {
                    try { widgetCount = std::stoi(val); } catch (...) {}
                    if (widgetCount > 0) loadedWidgets.resize(widgetCount);
                } else if (key.rfind("w", 0) == 0) {
                    // Format: w<i>_<property> or w<i>l<j>_<property>
                    size_t us = key.find('_');
                    if (us != std::string::npos) {
                        std::string idxStr = key.substr(1, us - 1);
                        std::string prop = key.substr(us + 1);
                        size_t lPos = idxStr.find('l');
                        if (lPos == std::string::npos) {
                            // Widget level property
                            try {
                                int wIdx = std::stoi(idxStr);
                                if (wIdx >= 0 && wIdx < (int)loadedWidgets.size()) {
                                    if (prop == "type") loadedWidgets[wIdx].isGraph = (val == "graph");
                                    else if (prop == "graphType") loadedWidgets[wIdx].isHistogram = (val == "histogram");
                                    else if (prop == "rangeMin") loadedWidgets[wIdx].rangeMin = std::stof(val);
                                    else if (prop == "rangeMax") loadedWidgets[wIdx].rangeMax = std::stof(val);
                                    else if (prop == "autoScale") loadedWidgets[wIdx].autoScale = (val == "true" || val == "1");
                                    else if (prop == "lineCount") {
                                        int lc = std::stoi(val);
                                        if (lc > 0) loadedWidgets[wIdx].lines.resize(lc);
                                    }
                                }
                            } catch (...) {}
                        } else {
                            // Line level property: w<i>l<j>
                            try {
                                int wIdx = std::stoi(idxStr.substr(0, lPos));
                                int lIdx = std::stoi(idxStr.substr(lPos + 1));
                                if (wIdx >= 0 && wIdx < (int)loadedWidgets.size()) {
                                    if (lIdx >= (int)loadedWidgets[wIdx].lines.size()) {
                                        loadedWidgets[wIdx].lines.resize(lIdx + 1);
                                    }
                                    auto& line = loadedWidgets[wIdx].lines[lIdx];
                                    if (prop == "metricId") {
                                        line.metricId = std::stoi(val);
                                        PopulateMetricMetadata(line);
                                    } else if (prop == "statId") {
                                        line.statId = std::stoi(val);
                                    } else if (prop == "lineColor") {
                                        ParseColorHex(val, line.r, line.g, line.b, line.a);
                                    } else if (prop == "fillColor") {
                                        ParseColorHex(val, line.fillR, line.fillG, line.fillB, line.fillA);
                                    }
                                }
                            } catch (...) {}
                        }
                    }
                }
            }
        }

        if (!loadedWidgets.empty()) {
            activeWidgets_ = std::move(loadedWidgets);
        }
    }

    static void ParseColorHex(const std::string& hex, float& r, float& g, float& b, float& a) {
        if (hex.size() >= 7 && hex[0] == '#') {
            unsigned int ir = 0, ig = 0, ib = 0, ia = 255;
            if (hex.size() == 9) {
                sscanf(hex.c_str() + 1, "%02x%02x%02x%02x", &ia, &ir, &ig, &ib);
            } else if (hex.size() == 7) {
                sscanf(hex.c_str() + 1, "%02x%02x%02x", &ir, &ig, &ib);
            }
            r = ir / 255.0f;
            g = ig / 255.0f;
            b = ib / 255.0f;
            a = ia / 255.0f;
        }
    }

    static void PopulateMetricMetadata(OverlayMetricLine& line) {
        switch (line.metricId) {
            case PM_METRIC_BETWEEN_DISPLAY_CHANGE: line.label = "Between Display Change"; line.units = "ms"; break;
            case PM_METRIC_UNTIL_DISPLAYED: line.label = "Until Displayed"; line.units = "ms"; break;
            case PM_METRIC_DROPPED_FRAMES: line.label = "Dropped Frames"; line.units = ""; break;
            case PM_METRIC_APPLICATION_FPS: line.label = "Application FPS"; line.units = "FPS"; break;
            case PM_METRIC_DISPLAYED_FPS: line.label = "Displayed FPS"; line.units = "FPS"; break;
            case PM_METRIC_PRESENTED_FPS: line.label = "Presented FPS"; line.units = "FPS"; break;
            case PM_METRIC_PRESENTED_FRAME_TIME: line.label = "Presented Frame Time"; line.units = "ms"; break;
            case PM_METRIC_DISPLAYED_FRAME_TIME: line.label = "Displayed Frame Time"; line.units = "ms"; break;
            case PM_METRIC_BETWEEN_PRESENTS: line.label = "Between Presents"; line.units = "ms"; break;
            case PM_METRIC_IN_PRESENT_API: line.label = "In Present API"; line.units = "ms"; break;
            case PM_METRIC_BETWEEN_APP_START: line.label = "Between App Start"; line.units = "ms"; break;
            case PM_METRIC_BETWEEN_SIMULATION_START: line.label = "Between Sim Start"; line.units = "ms"; break;
            case PM_METRIC_ANIMATION_ERROR: line.label = "Animation Error"; line.units = "ms"; break;
            case PM_METRIC_ANIMATION_TIME: line.label = "Animation Time"; line.units = "ms"; break;
            case PM_METRIC_FLIP_DELAY: line.label = "Flip Delay"; line.units = "ms"; break;
            case PM_METRIC_SYNC_INTERVAL: line.label = "Sync Interval"; line.units = ""; break;
            case PM_METRIC_ALLOWS_TEARING: line.label = "Allows Tearing"; line.units = ""; break;
            case PM_METRIC_PRESENT_MODE: line.label = "Present Mode"; line.units = ""; break;
            case PM_METRIC_PRESENT_RUNTIME: line.label = "Present Runtime"; line.units = ""; break;
            case PM_METRIC_FRAME_TYPE: line.label = "Frame Type"; line.units = ""; break;

            case PM_METRIC_DISPLAY_LATENCY: line.label = "Display Latency"; line.units = "ms"; break;
            case PM_METRIC_CLICK_TO_PHOTON_LATENCY: line.label = "Click to Photon Latency"; line.units = "ms"; break;
            case PM_METRIC_ALL_INPUT_TO_PHOTON_LATENCY: line.label = "All Input to Photon"; line.units = "ms"; break;
            case PM_METRIC_INSTRUMENTED_LATENCY: line.label = "Instrumented Latency"; line.units = "ms"; break;
            case PM_METRIC_PC_LATENCY: line.label = "PC Latency"; line.units = "ms"; break;
            case PM_METRIC_GPU_LATENCY: line.label = "GPU Latency"; line.units = "ms"; break;
            case PM_METRIC_RENDER_PRESENT_LATENCY: line.label = "Render Present Latency"; line.units = "ms"; break;

            case PM_METRIC_GPU_TIME: line.label = "GPU Time"; line.units = "ms"; break;
            case PM_METRIC_GPU_BUSY: line.label = "GPU Busy"; line.units = "ms"; break;
            case PM_METRIC_GPU_WAIT: line.label = "GPU Wait"; line.units = "ms"; break;
            case PM_METRIC_GPU_UTILIZATION: line.label = "GPU Utilization"; line.units = "%"; break;
            case PM_METRIC_GPU_RENDER_COMPUTE_UTILIZATION: line.label = "GPU Render/Compute"; line.units = "%"; break;
            case PM_METRIC_GPU_MEDIA_UTILIZATION: line.label = "GPU Media Util"; line.units = "%"; break;
            case PM_METRIC_GPU_POWER: line.label = "GPU Power"; line.units = "W"; break;
            case PM_METRIC_GPU_CARD_POWER: line.label = "GPU Card Power"; line.units = "W"; break;
            case PM_METRIC_GPU_SUSTAINED_POWER_LIMIT: line.label = "GPU Sustained Power Limit"; line.units = "W"; break;
            case PM_METRIC_GPU_VOLTAGE: line.label = "GPU Voltage"; line.units = "V"; break;
            case PM_METRIC_GPU_FREQUENCY: line.label = "GPU Frequency"; line.units = "MHz"; break;
            case PM_METRIC_GPU_EFFECTIVE_FREQUENCY: line.label = "GPU Effective Freq"; line.units = "MHz"; break;
            case PM_METRIC_GPU_TEMPERATURE: line.label = "GPU Temperature"; line.units = "C"; break;
            case PM_METRIC_GPU_VOLTAGE_REGULATOR_TEMPERATURE: line.label = "GPU VRM Temp"; line.units = "C"; break;
            case PM_METRIC_GPU_FAN_SPEED: line.label = "GPU Fan Speed"; line.units = "RPM"; break;
            case PM_METRIC_GPU_NAME: line.label = "GPU Name"; line.units = ""; break;
            case PM_METRIC_GPU_VENDOR: line.label = "GPU Vendor"; line.units = ""; break;

            case PM_METRIC_GPU_MEM_USED: line.label = "GPU VRAM Used"; line.units = "GB"; break;
            case PM_METRIC_GPU_MEM_SIZE: line.label = "GPU VRAM Total Size"; line.units = "GB"; break;
            case PM_METRIC_GPU_MEM_UTILIZATION: line.label = "GPU VRAM Utilization"; line.units = "%"; break;
            case PM_METRIC_GPU_MEM_POWER: line.label = "GPU VRAM Power"; line.units = "W"; break;
            case PM_METRIC_GPU_MEM_VOLTAGE: line.label = "GPU VRAM Voltage"; line.units = "V"; break;
            case PM_METRIC_GPU_MEM_FREQUENCY: line.label = "GPU VRAM Frequency"; line.units = "MHz"; break;
            case PM_METRIC_GPU_MEM_EFFECTIVE_FREQUENCY: line.label = "GPU VRAM Effective Freq"; line.units = "MHz"; break;
            case PM_METRIC_GPU_MEM_TEMPERATURE: line.label = "GPU VRAM Temp"; line.units = "C"; break;
            case PM_METRIC_GPU_MEM_MAX_BANDWIDTH: line.label = "GPU VRAM Max Bandwidth"; line.units = "GB/s"; break;
            case PM_METRIC_GPU_MEM_WRITE_BANDWIDTH: line.label = "GPU VRAM Write Bandwidth"; line.units = "GB/s"; break;
            case PM_METRIC_GPU_MEM_READ_BANDWIDTH: line.label = "GPU VRAM Read Bandwidth"; line.units = "GB/s"; break;
            case PM_METRIC_GPU_MEM_EFFECTIVE_BANDWIDTH: line.label = "GPU VRAM Eff Bandwidth"; line.units = "GB/s"; break;

            case PM_METRIC_GPU_POWER_LIMITED: line.label = "GPU Power Limited"; line.units = ""; break;
            case PM_METRIC_GPU_TEMPERATURE_LIMITED: line.label = "GPU Temp Limited"; line.units = ""; break;
            case PM_METRIC_GPU_CURRENT_LIMITED: line.label = "GPU Current Limited"; line.units = ""; break;
            case PM_METRIC_GPU_VOLTAGE_LIMITED: line.label = "GPU Voltage Limited"; line.units = ""; break;
            case PM_METRIC_GPU_UTILIZATION_LIMITED: line.label = "GPU Util Limited"; line.units = ""; break;

            case PM_METRIC_CPU_UTILIZATION: line.label = "CPU Utilization"; line.units = "%"; break;
            case PM_METRIC_CPU_BUSY: line.label = "CPU Busy"; line.units = "ms"; break;
            case PM_METRIC_CPU_WAIT: line.label = "CPU Wait"; line.units = "ms"; break;
            case PM_METRIC_CPU_FRAME_TIME: line.label = "CPU Frame Time"; line.units = "ms"; break;
            case PM_METRIC_CPU_POWER: line.label = "CPU Power"; line.units = "W"; break;
            case PM_METRIC_CPU_POWER_LIMIT: line.label = "CPU Power Limit"; line.units = "W"; break;
            case PM_METRIC_CPU_TEMPERATURE: line.label = "CPU Temperature"; line.units = "C"; break;
            case PM_METRIC_CPU_CORE_TEMPERATURE: line.label = "CPU Core Temp"; line.units = "C"; break;
            case PM_METRIC_CPU_FREQUENCY: line.label = "CPU Frequency"; line.units = "MHz"; break;
            case PM_METRIC_CPU_CORE_UTILITY: line.label = "CPU Core Utility"; line.units = "%"; break;
            case PM_METRIC_CPU_NAME: line.label = "CPU Name"; line.units = ""; break;
            case PM_METRIC_CPU_VENDOR: line.label = "CPU Vendor"; line.units = ""; break;

            case PM_METRIC_PSO_COMPILE_COUNT: line.label = "PSO Compile Count"; line.units = ""; break;
            case PM_METRIC_PSO_COMPILE_TIME: line.label = "PSO Compile Time"; line.units = "ms"; break;
            case PM_METRIC_PSO_COMPILE_BUSY_PERCENT: line.label = "PSO Compile Busy %"; line.units = "%"; break;

            default:  line.label = "Metric"; line.units = ""; break;
        }
    }

    void InitDefaultWidgets() {
        activeWidgets_.clear();

        // Widget 0: Until Displayed (avg, raw) - Graph
        {
            OverlayWidgetSpec w;
            w.isGraph = true;
            w.rangeMin = 0.0f;
            w.rangeMax = 1.0f;
            w.autoScale = true;

            OverlayMetricLine mAvg;
            mAvg.metricId = PM_METRIC_UNTIL_DISPLAYED;
            mAvg.statId = 1;
            mAvg.label = "Until Displayed";
            mAvg.units = "ms";
            mAvg.r = 0.0f; mAvg.g = 0.90f; mAvg.b = 1.0f; mAvg.a = 1.0f;
            mAvg.fillR = 0.0f; mAvg.fillG = 0.85f; mAvg.fillB = 1.0f; mAvg.fillA = 0.25f;
            w.lines.push_back(mAvg);

            OverlayMetricLine mRaw;
            mRaw.metricId = PM_METRIC_UNTIL_DISPLAYED;
            mRaw.statId = 4;
            mRaw.label = "Until Displayed";
            mRaw.units = "ms";
            mRaw.r = 0.40f; mRaw.g = 0.95f; mRaw.b = 0.65f; mRaw.a = 1.0f;
            w.lines.push_back(mRaw);

            activeWidgets_.push_back(w);
        }

        // Widget 1: Between Display Change (avg, 99%, raw) - Graph
        {
            OverlayWidgetSpec w;
            w.isGraph = true;
            w.rangeMin = 9.0f;
            w.rangeMax = 11.0f;
            w.autoScale = true;

            OverlayMetricLine mAvg;
            mAvg.metricId = PM_METRIC_BETWEEN_DISPLAY_CHANGE;
            mAvg.statId = 1;
            mAvg.label = "Between Display Change";
            mAvg.units = "ms";
            mAvg.r = 0.0f; mAvg.g = 0.90f; mAvg.b = 1.0f; mAvg.a = 1.0f;
            mAvg.fillR = 0.0f; mAvg.fillG = 0.85f; mAvg.fillB = 1.0f; mAvg.fillA = 0.25f;
            w.lines.push_back(mAvg);

            OverlayMetricLine m99;
            m99.metricId = PM_METRIC_BETWEEN_DISPLAY_CHANGE;
            m99.statId = 6;
            m99.label = "Between Display Change";
            m99.units = "ms";
            m99.r = 1.0f; m99.g = 0.35f; m99.b = 0.35f; m99.a = 1.0f;
            w.lines.push_back(m99);

            OverlayMetricLine mRaw;
            mRaw.metricId = PM_METRIC_BETWEEN_DISPLAY_CHANGE;
            mRaw.statId = 4;
            mRaw.label = "Between Display Change";
            mRaw.units = "ms";
            mRaw.r = 0.40f; mRaw.g = 0.95f; mRaw.b = 0.65f; mRaw.a = 1.0f;
            w.lines.push_back(mRaw);

            activeWidgets_.push_back(w);
        }

        // Widget 2: Dropped Frames (avg) - Graph
        {
            OverlayWidgetSpec w;
            w.isGraph = true;
            w.rangeMin = 0.0f;
            w.rangeMax = 1.0f;
            w.autoScale = true;

            OverlayMetricLine mAvg;
            mAvg.metricId = PM_METRIC_DROPPED_FRAMES;
            mAvg.statId = 1;
            mAvg.label = "Dropped Frames";
            mAvg.units = "";
            mAvg.r = 0.0f; mAvg.g = 0.90f; mAvg.b = 1.0f; mAvg.a = 1.0f;
            mAvg.fillR = 0.0f; mAvg.fillG = 0.85f; mAvg.fillB = 1.0f; mAvg.fillA = 0.15f;
            w.lines.push_back(mAvg);

            activeWidgets_.push_back(w);
        }
    }

    void GenerateHudVertices(std::vector<OverlayVertex>& verts, uint32_t sw, uint32_t sh, int corner,
                             double presentFps, double displayedFps, double fps1PercentLow,
                             double frameTimeMs, double latencyMs, double animErrorMs,
                             bool isRecording,
                             const ipc::TelemetrySnapshot* telem = nullptr,
                             bool hudVisible = true)
    {
        uint64_t nowNs = gnumon::common::Clock::GetTimestampNs();
        CheckReloadConfig(nowNs);
        RecordMetrics(presentFps, displayedFps, frameTimeMs, latencyMs, animErrorMs, telem);

        float uiScale = std::clamp(std::min(static_cast<float>(sw) / 1920.0f, static_cast<float>(sh) / 1080.0f), 0.85f, 2.0f);
        char buf[128];

        // 0. Render OSD Toast Notification if active
        if (nowNs < toastExpiryNs_ && !toastTitle_.empty()) {
            float toastW = 480.0f * uiScale;
            float toastH = 46.0f * uiScale;
            float toastX = (static_cast<float>(sw) - toastW) * 0.5f;
            float toastY = 24.0f * uiScale;

            AddQuad(verts, toastX - 1.5f, toastY - 1.5f, toastW + 3.0f, toastH + 3.0f, 0.0f, 0.90f, 0.55f, 0.85f);
            AddQuad(verts, toastX, toastY, toastW, toastH, 0.05f, 0.07f, 0.10f, 0.95f);
            AddQuad(verts, toastX, toastY + toastH - 2.5f * uiScale, toastW, 2.5f * uiScale, 0.0f, 0.90f, 0.55f, 0.95f);

            AddString(verts, toastTitle_.c_str(), toastX + 14.0f * uiScale, toastY + 8.0f * uiScale, 0.82f * uiScale, 0.0f, 0.90f, 0.55f, 1.0f);
            AddString(verts, toastMessage_.c_str(), toastX + 14.0f * uiScale, toastY + 26.0f * uiScale, 0.70f * uiScale, 0.85f, 0.90f, 0.95f, 0.90f);
        }

        if (!hudVisible) return;

        int activeCorner = (configuredCorner_ >= 0) ? configuredCorner_ : corner;
        float margin = 20.0f * uiScale;

        // 1. Minimal pill mode if preset 0
        if (hudPreset_ == 0) {
            float compW = 440.0f * uiScale;
            float compH = 34.0f * uiScale;
            float compX = margin;
            float compY = margin;
            if (activeCorner == 1 || activeCorner == 3) compX = static_cast<float>(sw) - compW - margin;
            if (activeCorner == 2 || activeCorner == 3) compY = static_cast<float>(sh) - compH - margin;

            AddQuad(verts, compX - 1.0f, compY - 1.0f, compW + 2.0f, compH + 2.0f, 0.0f, 0.74f, 0.83f, 0.40f);
            AddQuad(verts, compX, compY, compW, compH, 0.04f, 0.06f, 0.09f, 0.88f);
            AddQuad(verts, compX, compY, compW, 1.5f * uiScale, 0.0f, 0.74f, 0.83f, 0.90f);

            float curX = compX + 10.0f * uiScale;
            float rY = compY + 8.0f * uiScale;
            curX += AddString(verts, "GNUMON", curX, rY, 0.78f * uiScale, 0.0f, 0.90f, 0.55f, 1.0f) + 8.0f * uiScale;

            snprintf(buf, sizeof(buf), "%.0f FPS", displayedFps > 0.0 ? displayedFps : presentFps);
            curX += AddString(verts, buf, curX, rY, 0.82f * uiScale, 1.0f, 1.0f, 1.0f, 1.0f) + 6.0f * uiScale;

            snprintf(buf, sizeof(buf), "%.1f ms", frameTimeMs);
            curX += AddString(verts, buf, curX, rY, 0.75f * uiScale, 0.40f, 0.85f, 0.95f, 0.90f) + 8.0f * uiScale;

            if (isRecording) {
                AddString(verts, "[REC]", curX, rY, 0.75f * uiScale, 1.0f, 0.25f, 0.25f, 1.0f);
            }
            return;
        }

        // 2. Full Multi-Metric Dynamic Loadout Overlay (matching PresentMon Windows!)
        float cardW = (configuredWidth_ > 200.0f ? configuredWidth_ : 440.0f) * uiScale;

        // Calculate card height dynamically from active widgets
        float totalH = 34.0f * uiScale; // Header
        for (const auto& w : activeWidgets_) {
            if (w.isGraph) {
                totalH += (w.lines.size() * 18.0f * uiScale) + 76.0f * uiScale + 22.0f * uiScale;
            } else {
                totalH += 22.0f * uiScale;
            }
        }
        totalH += 10.0f * uiScale;
        float cardH = std::clamp(totalH, 100.0f, static_cast<float>(sh) - margin * 2.0f);

        float cardX = margin;
        float cardY = margin;
        if (activeCorner == 1 || activeCorner == 3) cardX = static_cast<float>(sw) - cardW - margin;
        if (activeCorner == 2 || activeCorner == 3) cardY = static_cast<float>(sh) - cardH - margin;

        // Card backdrop + top glow border
        AddQuad(verts, cardX - 2.0f * uiScale, cardY - 2.0f * uiScale,
                cardW + 4.0f * uiScale, cardH + 4.0f * uiScale, 0.0f, 0.74f, 0.83f, 0.35f);
        AddQuad(verts, cardX, cardY, cardW, cardH, 0.03f, 0.05f, 0.08f, 0.92f);
        AddQuad(verts, cardX, cardY, cardW, 2.0f * uiScale, 0.0f, 0.74f, 0.83f, 0.95f);

        float padX = cardX + 14.0f * uiScale;
        float curY = cardY + 8.0f * uiScale;

        // --- TITLE BAR ---
        float curX = padX;
        curX += AddString(verts, "GNUMON", curX, curY, 0.86f * uiScale, 0.0f, 0.90f, 0.55f, 1.0f) + 6.0f * uiScale;
        curX += AddString(verts, "PRESENTMON", curX, curY, 0.80f * uiScale, 0.55f, 0.65f, 0.75f, 0.90f) + 12.0f * uiScale;
        if (isRecording) {
            AddString(verts, "[* REC]", curX, curY, 0.82f * uiScale, 1.0f, 0.25f, 0.25f, 1.0f);
        } else {
            AddString(verts, "[LIVE]", curX, curY, 0.76f * uiScale, 0.40f, 0.80f, 0.90f, 0.85f);
        }
        curY += 24.0f * uiScale;

        // --- WIDGETS ---
        for (const auto& w : activeWidgets_) {
            if (!w.isGraph) {
                // Readout Widget
                if (w.lines.empty()) continue;
                const auto& line = w.lines[0];
                const auto& hist = GetHistoryForMetric(line.metricId);
                float val = CalculateStat(hist, line.statId);

                // Swatch quad
                AddQuad(verts, padX, curY + 2.0f * uiScale, 8.0f * uiScale, 8.0f * uiScale, line.r, line.g, line.b, line.a);

                // Label
                const char* statName = (line.statId == 1) ? "(avg)" : (line.statId == 6) ? "(99%)" : (line.statId == 5) ? "(1%)" : (line.statId == 4) ? "(raw)" : "";
                snprintf(buf, sizeof(buf), "%s %s", line.label.c_str(), statName);
                AddString(verts, buf, padX + 14.0f * uiScale, curY, 0.75f * uiScale, 0.85f, 0.90f, 0.95f, 0.95f);

                // Value + Units
                if (!line.units.empty()) {
                    snprintf(buf, sizeof(buf), "%.1f %s", val, line.units.c_str());
                } else {
                    snprintf(buf, sizeof(buf), "%.2f", val);
                }
                AddString(verts, buf, cardX + cardW - 120.0f * uiScale, curY, 0.78f * uiScale, line.r, line.g, line.b, 1.0f);

                curY += 20.0f * uiScale;
            } else {
                // Graph Widget (faithful to PresentMon Windows screenshot!)
                // 1. Swatches & Header readouts for each line
                for (const auto& line : w.lines) {
                    const auto& hist = GetHistoryForMetric(line.metricId);
                    float val = CalculateStat(hist, line.statId);

                    // Swatch quad
                    AddQuad(verts, padX, curY + 3.0f * uiScale, 8.0f * uiScale, 8.0f * uiScale, line.r, line.g, line.b, line.a);

                    // Metric label with stat (e.g. "Until Displayed (avg)")
                    const char* statName = (line.statId == 1) ? "(avg)" : (line.statId == 6) ? "(99%)" : (line.statId == 5) ? "(1%)" : (line.statId == 4) ? "(raw)" : "";
                    snprintf(buf, sizeof(buf), "%s %s", line.label.c_str(), statName);
                    AddString(verts, buf, padX + 14.0f * uiScale, curY, 0.76f * uiScale, 0.88f, 0.92f, 0.96f, 0.95f);

                    // Value + Units (e.g. "0.39 ms")
                    if (!line.units.empty()) {
                        snprintf(buf, sizeof(buf), "%.2f %s", val, line.units.c_str());
                    } else {
                        snprintf(buf, sizeof(buf), "%.2f", val);
                    }
                    AddString(verts, buf, cardX + cardW - 130.0f * uiScale, curY, 0.78f * uiScale, line.r, line.g, line.b, 1.0f);

                    curY += 17.0f * uiScale;
                }

                // 2. Plot Box Dimensions
                float gx = cardX + 38.0f * uiScale; // room on left for Y axis labels
                float gw = cardW - 52.0f * uiScale;
                float gh = 64.0f * uiScale;
                float gy = curY + 2.0f * uiScale;

                // Box background & subtle crisp border
                AddQuad(verts, gx - 1.0f, gy - 1.0f, gw + 2.0f, gh + 2.0f, 0.16f, 0.22f, 0.32f, 0.60f);
                AddQuad(verts, gx, gy, gw, gh, 0.02f, 0.04f, 0.07f, 0.92f);

                // Grid lines (horizontal & vertical)
                for (int step = 1; step <= 3; ++step) {
                    float yGrid = gy + (gh * step) / 4.0f;
                    AddQuad(verts, gx, yGrid, gw, 1.0f * uiScale, 0.16f, 0.25f, 0.36f, 0.35f);
                }
                for (int step = 1; step <= 9; ++step) {
                    float xGrid = gx + (gw * step) / 10.0f;
                    AddQuad(verts, xGrid, gy, 1.0f * uiScale, gh, 0.16f, 0.25f, 0.36f, 0.25f);
                }

                // Compute Y range (autoscale or fixed)
                float yMin = w.rangeMin;
                float yMax = w.rangeMax;
                if (w.autoScale) {
                    float calcMin = 1e9f;
                    float calcMax = -1e9f;
                    for (const auto& line : w.lines) {
                        const auto& hist = GetHistoryForMetric(line.metricId);
                        for (float v : hist) {
                            if (v < calcMin) calcMin = v;
                            if (v > calcMax) calcMax = v;
                        }
                    }
                    if (calcMax > calcMin) {
                        float pad = (calcMax - calcMin) * 0.15f;
                        yMin = std::max(0.0f, calcMin - pad);
                        yMax = calcMax + pad;
                    } else if (calcMax >= 0.0f) {
                        yMin = 0.0f;
                        yMax = std::max(1.0f, calcMax * 1.2f);
                    }
                }
                if (std::abs(yMax - yMin) < 0.001f) {
                    yMax = yMin + 1.0f;
                }

                auto getPlotY = [&](float val) -> float {
                    float norm = (val - yMin) / (yMax - yMin);
                    norm = std::clamp(norm, 0.0f, 1.0f);
                    return (gy + gh) - norm * (gh - 4.0f) - 2.0f;
                };

                // Left Y axis labels
                if (yMax >= 10.0f) snprintf(buf, sizeof(buf), "%.0f", yMax);
                else snprintf(buf, sizeof(buf), "%.1f", yMax);
                AddString(verts, buf, cardX + 6.0f * uiScale, gy, 0.62f * uiScale, 0.65f, 0.75f, 0.85f, 0.90f);

                if (yMin >= 10.0f) snprintf(buf, sizeof(buf), "%.0f", yMin);
                else snprintf(buf, sizeof(buf), "%.1f", yMin);
                AddString(verts, buf, cardX + 6.0f * uiScale, gy + gh - 9.0f * uiScale, 0.62f * uiScale, 0.65f, 0.75f, 0.85f, 0.90f);

                // Bottom X axis labels ("10" on left, "0" on right)
                AddString(verts, "10", gx + 2.0f * uiScale, gy + gh + 3.0f * uiScale, 0.62f * uiScale, 0.65f, 0.75f, 0.85f, 0.85f);
                AddString(verts, "0", gx + gw - 12.0f * uiScale, gy + gh + 3.0f * uiScale, 0.62f * uiScale, 0.65f, 0.75f, 0.85f, 0.85f);

                // 3. Render Area Fill and Series Lines
                for (size_t lIdx = 0; lIdx < w.lines.size(); ++lIdx) {
                    const auto& line = w.lines[lIdx];
                    const auto& hist = GetHistoryForMetric(line.metricId);
                    if (hist.size() < 2) continue;

                    size_t nSamples = hist.size();
                    float stepX = gw / 127.0f;
                    float startX = (gx + gw) - (static_cast<float>(nSamples - 1) * stepX);

                    // Area fill under curve for primary series (lIdx == 0)
                    if (lIdx == 0 && line.fillA > 0.01f) {
                        float bottomY = gy + gh;
                        for (size_t i = 0; i < nSamples - 1; ++i) {
                            float px0 = startX + static_cast<float>(i) * stepX;
                            float py0 = getPlotY(hist[i]);
                            float px1 = startX + static_cast<float>(i + 1) * stepX;
                            float py1 = getPlotY(hist[i + 1]);

                            OverlayVertex f0{px0, py0, -1.0f, -1.0f, line.fillR, line.fillG, line.fillB, line.fillA};
                            OverlayVertex f1{px1, py1, -1.0f, -1.0f, line.fillR, line.fillG, line.fillB, line.fillA};
                            OverlayVertex f2{px1, bottomY, -1.0f, -1.0f, line.fillR, line.fillG, line.fillB, line.fillA * 0.1f};
                            OverlayVertex f3{px0, bottomY, -1.0f, -1.0f, line.fillR, line.fillG, line.fillB, line.fillA * 0.1f};
                            verts.push_back(f0); verts.push_back(f1); verts.push_back(f2);
                            verts.push_back(f0); verts.push_back(f2); verts.push_back(f3);
                        }
                    }

                    // Series Line Curve
                    bool isStepped = (line.statId == 6 || line.statId == 5); // 99% or 1% percentiles step
                    for (size_t i = 0; i < nSamples - 1; ++i) {
                        float px0 = startX + static_cast<float>(i) * stepX;
                        float py0 = getPlotY(hist[i]);
                        float px1 = startX + static_cast<float>(i + 1) * stepX;
                        float py1 = getPlotY(hist[i + 1]);

                        if (isStepped) {
                            // Stepped line: horizontal segment then vertical step
                            AddLine(verts, px0, py0, px1, py0, 1.8f * uiScale, line.r, line.g, line.b, line.a);
                            if (std::abs(py1 - py0) > 0.5f) {
                                AddLine(verts, px1, py0, px1, py1, 1.8f * uiScale, line.r, line.g, line.b, line.a);
                            }
                        } else {
                            // Direct line segment
                            AddLine(verts, px0, py0, px1, py1, 1.8f * uiScale, line.r, line.g, line.b, line.a);
                        }
                    }
                }

                curY = gy + gh + 18.0f * uiScale;
            }
        }
    }

protected:
    int hudPreset_ = 1; // 0 = Compact, 1 = Standard (Loadout Multi-Graph), 2 = Detailed
    int configuredCorner_ = -1;
    float configuredWidth_ = 440.0f;
    uint64_t lastConfigCheckNs_ = 0;
    time_t lastConfigMtime_ = 0;

    std::vector<OverlayWidgetSpec> activeWidgets_;

    // Multi-metric history buffers
    std::deque<float> frametimes_;
    std::deque<float> histUntilDisplayed_;
    std::deque<float> histDroppedFrames_;
    std::deque<float> histFps_;
    std::deque<float> histAnimError_;
    std::deque<float> histGpuUtil_;
    std::deque<float> histGpuPower_;
    std::deque<float> histGpuTemp_;
    std::deque<float> histGpuFreq_;
    std::deque<float> histVramUsed_;
    std::deque<float> histCpuUtil_;
    std::deque<float> histCpuPower_;
    std::deque<float> histCpuTemp_;
    std::deque<float> histCpuFreq_;

    std::string toastTitle_;
    std::string toastMessage_;
    uint64_t toastExpiryNs_ = 0;

    std::string hotkeyOverlay_ = "Ctrl+Shift+O";
    std::string hotkeyPresetCycle_ = "Ctrl+Shift+P";
    std::string hotkeyCapture_ = "Ctrl+Shift+K";

public:
    std::string GetHotkeyOverlay() const { return hotkeyOverlay_; }
    std::string GetHotkeyPresetCycle() const { return hotkeyPresetCycle_; }
    std::string GetHotkeyCapture() const { return hotkeyCapture_; }
};

} // namespace gnumon::layer
