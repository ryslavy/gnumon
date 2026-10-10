#pragma once

#include <vector>
#include <string>
#include <cstring>
#include <cmath>
#include <cstdio>
#include <algorithm>
#include <deque>
#include <cstdint>
#include "font_atlas.hpp"
#include "../ipc/FrameRingBuffer.h"
#include "../common/Clock.h"

namespace gnumon::layer {

struct OverlayVertex {
    float x, y;
    float u, v;
    float r, g, b, a;
};

class HudVertexGenerator {
public:
    HudVertexGenerator() = default;
    virtual ~HudVertexGenerator() = default;

    void SetPreset(int preset) {
        hudPreset_ = std::clamp(preset, 0, 2);
    }

    int GetPreset() const {
        return hudPreset_;
    }

    void AddFrametimeSample(float ms) {
        if (ms < 0.01f) ms = 0.01f;
        if (ms > 500.0f) ms = 500.0f;
        frametimes_.push_back(ms);
        if (frametimes_.size() > 128) {
            frametimes_.pop_front();
        }
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

    void GenerateHudVertices(std::vector<OverlayVertex>& verts, uint32_t sw, uint32_t sh, int corner,
                             double presentFps, double displayedFps, double fps1PercentLow,
                             double frameTimeMs, double latencyMs, double animErrorMs,
                             bool isRecording,
                             const ipc::TelemetrySnapshot* telem = nullptr,
                             bool hudVisible = true)
    {
        bool hasTelem = (telem && telem->valid != 0);
        float uiScale = std::clamp(std::min(static_cast<float>(sw) / 1920.0f, static_cast<float>(sh) / 1080.0f), 0.85f, 2.0f);
        char buf[128];

        // 0. Render OSD Toast Notification if active
        uint64_t nowNs = gnumon::common::Clock::GetTimestampNs();
        if (nowNs < toastExpiryNs_ && !toastTitle_.empty()) {
            float toastW = 480.0f * uiScale;
            float toastH = 46.0f * uiScale;
            float toastX = (static_cast<float>(sw) - toastW) * 0.5f;
            float toastY = 24.0f * uiScale;

            // Toast backdrop & glowing border
            AddQuad(verts, toastX - 1.5f, toastY - 1.5f, toastW + 3.0f, toastH + 3.0f, 0.0f, 0.90f, 0.55f, 0.85f);
            AddQuad(verts, toastX, toastY, toastW, toastH, 0.05f, 0.07f, 0.10f, 0.95f);
            AddQuad(verts, toastX, toastY + toastH - 2.5f * uiScale, toastW, 2.5f * uiScale, 0.0f, 0.90f, 0.55f, 0.95f);

            AddString(verts, toastTitle_.c_str(), toastX + 14.0f * uiScale, toastY + 8.0f * uiScale, 0.82f * uiScale, 0.0f, 0.90f, 0.55f, 1.0f);
            AddString(verts, toastMessage_.c_str(), toastX + 14.0f * uiScale, toastY + 26.0f * uiScale, 0.70f * uiScale, 0.85f, 0.90f, 0.95f, 0.90f);
        }

        if (!hudVisible) {
            return;
        }

        float margin = 20.0f * uiScale;

        // 1. If Compact preset (hudPreset_ == 0), render sleek minimal pill HUD
        if (hudPreset_ == 0) {
            float compW = 440.0f * uiScale;
            float compH = 34.0f * uiScale;
            float compX = margin;
            float compY = margin;
            if (corner == 1 || corner == 3) compX = static_cast<float>(sw) - compW - margin;
            if (corner == 2 || corner == 3) compY = static_cast<float>(sh) - compH - margin;

            AddQuad(verts, compX - 1.0f, compY - 1.0f, compW + 2.0f, compH + 2.0f, 0.0f, 0.74f, 0.83f, 0.40f);
            AddQuad(verts, compX, compY, compW, compH, 0.04f, 0.06f, 0.09f, 0.88f);
            AddQuad(verts, compX, compY, compW, 1.5f * uiScale, 0.0f, 0.74f, 0.83f, 0.90f);

            float curX = compX + 10.0f * uiScale;
            float rY = compY + 8.0f * uiScale;
            curX += AddString(verts, "GNUMON", curX, rY, 0.78f * uiScale, 0.0f, 0.90f, 0.55f, 1.0f) + 8.0f * uiScale;

            snprintf(buf, sizeof(buf), "%.0f FPS", displayedFps > 0.0 ? displayedFps : presentFps);
            curX += AddString(verts, buf, curX, rY, 0.82f * uiScale, 1.0f, 1.0f, 1.0f, 1.0f) + 6.0f * uiScale;

            if (fps1PercentLow > 0.1) {
                snprintf(buf, sizeof(buf), "(1%% %.0f)", fps1PercentLow);
                curX += AddString(verts, buf, curX, rY, 0.72f * uiScale, 1.0f, 0.75f, 0.20f, 0.90f) + 8.0f * uiScale;
            }

            snprintf(buf, sizeof(buf), "%.1f ms", frameTimeMs);
            curX += AddString(verts, buf, curX, rY, 0.75f * uiScale, 0.40f, 0.85f, 0.95f, 0.90f) + 8.0f * uiScale;

            if (hasTelem) {
                snprintf(buf, sizeof(buf), "GPU %.0f%% %.0fC", telem->gpuUtil, telem->gpuTemp);
                curX += AddString(verts, buf, curX, rY, 0.72f * uiScale, 0.35f, 0.85f, 1.0f, 0.90f) + 8.0f * uiScale;

                snprintf(buf, sizeof(buf), "CPU %.0f%%", telem->cpuUtil);
                curX += AddString(verts, buf, curX, rY, 0.72f * uiScale, 0.50f, 0.90f, 0.50f, 0.90f) + 8.0f * uiScale;
            }

            if (isRecording) {
                AddString(verts, "[REC]", curX, rY, 0.75f * uiScale, 1.0f, 0.25f, 0.25f, 1.0f);
            }
            return;
        }

        // Standard or Detailed Card
        float cardW = 460.0f * uiScale;
        float cardH = (hasTelem ? (hudPreset_ == 2 ? 248.0f : 226.0f) : 205.0f) * uiScale;

        float cardX = margin;
        float cardY = margin;
        if (corner == 1) { // Top-Right
            cardX = static_cast<float>(sw) - cardW - margin;
        } else if (corner == 2) { // Bottom-Left
            cardY = static_cast<float>(sh) - cardH - margin;
        } else if (corner == 3) { // Bottom-Right
            cardX = static_cast<float>(sw) - cardW - margin;
            cardY = static_cast<float>(sh) - cardH - margin;
        }

        // Premium dark card background + border glow
        AddQuad(verts, cardX - 2.0f * uiScale, cardY - 2.0f * uiScale,
                cardW + 4.0f * uiScale, cardH + 4.0f * uiScale, 0.0f, 0.74f, 0.83f, 0.35f);
        AddQuad(verts, cardX, cardY, cardW, cardH, 0.04f, 0.06f, 0.09f, 0.90f);
        AddQuad(verts, cardX, cardY, cardW, 2.0f * uiScale, 0.0f, 0.74f, 0.83f, 0.95f);

        float padX = cardX + 14.0f * uiScale;
        float gapX = 10.0f * uiScale;

        // --- ROW 1: Header / Title + Status Badge ---
        float r1_y = cardY + 10.0f * uiScale;
        float curX = padX;
        curX += AddString(verts, "GNUMON", curX, r1_y, 0.88f * uiScale, 0.0f, 0.90f, 0.55f, 1.0f) + 6.0f * uiScale;
        curX += AddString(verts, "PRESENTMON", curX, r1_y, 0.82f * uiScale, 0.55f, 0.65f, 0.75f, 0.90f) + gapX;

        if (isRecording) {
            AddString(verts, "[* REC]", curX, r1_y, 0.85f * uiScale, 1.0f, 0.25f, 0.25f, 1.0f);
        } else {
            AddString(verts, "[LIVE]", curX, r1_y, 0.78f * uiScale, 0.40f, 0.80f, 0.90f, 0.85f);
        }

        // --- ROW 2: Present FPS + Displayed FPS + 1% Low ---
        float r2_y = cardY + 34.0f * uiScale;
        curX = padX;

        snprintf(buf, sizeof(buf), "FPS: %.1f", displayedFps > 0.0 ? displayedFps : presentFps);
        curX += AddString(verts, buf, curX, r2_y, 0.95f * uiScale, 1.0f, 1.0f, 1.0f, 1.0f) + gapX;

        snprintf(buf, sizeof(buf), "(Pres: %.1f)", presentFps);
        curX += AddString(verts, buf, curX, r2_y, 0.80f * uiScale, 0.70f, 0.80f, 0.90f, 0.90f) + gapX;

        if (fps1PercentLow > 0.1) {
            snprintf(buf, sizeof(buf), "1%% Low: %.1f", fps1PercentLow);
            AddString(verts, buf, curX, r2_y, 0.82f * uiScale, 1.0f, 0.75f, 0.20f, 0.95f);
        }

        // --- ROW 3: FrameTime + Latency + Animation Error ---
        float r3_y = cardY + 56.0f * uiScale;
        curX = padX;

        snprintf(buf, sizeof(buf), "FT: %.2f ms", frameTimeMs);
        curX += AddString(verts, buf, curX, r3_y, 0.80f * uiScale, 0.40f, 0.85f, 0.95f, 0.95f) + gapX;

        snprintf(buf, sizeof(buf), "Lat: %.1f ms", latencyMs);
        curX += AddString(verts, buf, curX, r3_y, 0.80f * uiScale, 1.0f, 0.72f, 0.30f, 0.95f) + gapX;

        snprintf(buf, sizeof(buf), "AnimErr: %.2f ms", animErrorMs);
        AddString(verts, buf, curX, r3_y, 0.80f * uiScale, 0.90f, 0.50f, 0.50f, 0.95f);

        // --- ROW 4 (OPTIONAL): GPU & CPU Telemetry ---
        float gy = cardY + 76.0f * uiScale;
        if (hasTelem) {
            float r4_y = cardY + 76.0f * uiScale;
            curX = padX;
            snprintf(buf, sizeof(buf), "GPU: %.0f%% %.0fC %.0fW", telem->gpuUtil, telem->gpuTemp, telem->gpuPower);
            curX += AddString(verts, buf, curX, r4_y, 0.78f * uiScale, 0.35f, 0.85f, 1.0f, 0.95f) + gapX;

            snprintf(buf, sizeof(buf), "CPU: %.0f%% %.0fC %.0fW", telem->cpuUtil, telem->cpuTemp, telem->cpuPower);
            curX += AddString(verts, buf, curX, r4_y, 0.78f * uiScale, 0.50f, 0.90f, 0.50f, 0.95f) + gapX;

            if (telem->vramTotalGb > 0.1f) {
                snprintf(buf, sizeof(buf), "VRAM: %.1fG", telem->vramUsedGb);
                AddString(verts, buf, curX, r4_y, 0.78f * uiScale, 0.85f, 0.70f, 1.0f, 0.95f);
            }
            gy = cardY + 97.0f * uiScale;
        }

        if (hudPreset_ == 2 && hasTelem) {
            float rExtra_y = gy;
            curX = padX;
            snprintf(buf, sizeof(buf), "GPU Clk: %.0f MHz | CPU Clk: %.0f MHz", telem->gpuFreq, telem->cpuFreq);
            AddString(verts, buf, curX, rExtra_y, 0.75f * uiScale, 0.70f, 0.85f, 0.95f, 0.90f);
            gy += 21.0f * uiScale;
        }

        // --- ROW 5: Real-time Oscilloscope (Frametime Graph) ---
        float gx = cardX + 12.0f * uiScale;
        float gw = cardW - 24.0f * uiScale;
        float gh = 96.0f * uiScale;

        // Graph backdrop & crisp border
        AddQuad(verts, gx - 1.0f, gy - 1.0f, gw + 2.0f, gh + 2.0f, 0.20f, 0.24f, 0.30f, 0.55f);
        AddQuad(verts, gx, gy, gw, gh, 0.03f, 0.05f, 0.07f, 0.88f);

        // Reference gridlines:
        // Scale max frametime: minimum 40.0 ms, scaling up to 100.0 ms if large spikes exist
        float maxFt = 40.0f;
        for (float ft : frametimes_) {
            if (ft > maxFt) maxFt = ft;
        }
        if (maxFt > 100.0f) maxFt = 100.0f;

        auto getGraphY = [&](float ms) -> float {
            float clamped = std::clamp(ms, 0.0f, maxFt);
            return (gy + gh) - (clamped / maxFt) * (gh - 12.0f) - 6.0f;
        };

        // 16.66 ms line (60 FPS) in subtle green
        float y60 = getGraphY(16.66f);
        if (y60 > gy + 6.0f && y60 < gy + gh - 6.0f) {
            AddQuad(verts, gx + 2.0f, y60, gw - 4.0f, 1.0f * uiScale, 0.30f, 0.69f, 0.31f, 0.35f);
            AddString(verts, "16.6ms (60 FPS)", gx + 4.0f * uiScale, y60 - 8.0f * uiScale, 0.62f * uiScale, 0.30f, 0.69f, 0.31f, 0.70f);
        }

        // 33.33 ms line (30 FPS) in subtle red
        float y30 = getGraphY(33.33f);
        if (y30 > gy + 6.0f && y30 < gy + gh - 6.0f) {
            AddQuad(verts, gx + 2.0f, y30, gw - 4.0f, 1.0f * uiScale, 0.95f, 0.26f, 0.21f, 0.30f);
            AddString(verts, "33.3ms (30 FPS)", gx + 4.0f * uiScale, y30 - 8.0f * uiScale, 0.62f * uiScale, 0.95f, 0.26f, 0.21f, 0.65f);
        }

        // Draw rolling frametime curve & shaded glow
        size_t nSamples = frametimes_.size();
        if (nSamples >= 2) {
            float stepX = gw / 127.0f;
            float startX = (gx + gw) - (static_cast<float>(nSamples - 1) * stepX);

            for (size_t i = 0; i < nSamples - 1; ++i) {
                float px0 = startX + static_cast<float>(i) * stepX;
                float py0 = getGraphY(frametimes_[i]);
                float px1 = startX + static_cast<float>(i + 1) * stepX;
                float py1 = getGraphY(frametimes_[i + 1]);

                // Translucent fill under the curve
                float bottomY = gy + gh - 1.0f;
                OverlayVertex f0{px0, py0, -1.0f, -1.0f, 0.0f, 0.85f, 1.0f, 0.08f};
                OverlayVertex f1{px1, py1, -1.0f, -1.0f, 0.0f, 0.85f, 1.0f, 0.08f};
                OverlayVertex f2{px1, bottomY, -1.0f, -1.0f, 0.0f, 0.85f, 1.0f, 0.01f};
                OverlayVertex f3{px0, bottomY, -1.0f, -1.0f, 0.0f, 0.85f, 1.0f, 0.01f};
                verts.push_back(f0); verts.push_back(f1); verts.push_back(f2);
                verts.push_back(f0); verts.push_back(f2); verts.push_back(f3);

                // Sharp cyan line segment (1.8px * uiScale)
                AddLine(verts, px0, py0, px1, py1, 1.8f * uiScale, 0.0f, 0.90f, 1.0f, 0.95f);
            }
        }
    }

protected:
    int hudPreset_ = 1; // 0 = Compact, 1 = Standard (Oscilloscope), 2 = Detailed
    std::deque<float> frametimes_;
    std::string toastTitle_;
    std::string toastMessage_;
    uint64_t toastExpiryNs_ = 0;
};

} // namespace gnumon::layer
