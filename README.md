# gnumon

Linux-native port of Intel PresentMon providing real-time frame timing, GPU/CPU hardware telemetry, an in-game swapchain overlay with a frametime oscilloscope, and benchmark CSV capture for Vulkan and OpenGL applications.

## Overview

**gnumon** brings Intel PresentMon 2.x metrics and workflows to Linux with full C API compatibility (`PresentMonAPI.h`). It captures presentation timestamps directly from graphics APIs via a high-performance Vulkan layer and provides lock-free IPC, hardware telemetry (AMD, NVIDIA, Intel), an in-game HUD with a real-time frametime oscilloscope, and a Qt6 GUI with live graph analytics.

## Features

- **Frametime & Pacing Analysis**:
  - Displayed FPS, Presented FPS, and Application FPS (distinguishing native application frames from FSR 3 / DLSS 3 frame generation).
  - CPU frame time, GPU busy time, GPU wait time, render-to-display latency, and animation error.
  - 1% Low and 0.1% Low framerates.
- **In-Game Swapchain Overlay**:
  - Injected directly into the game's swapchain with zero compositor latency.
  - **Real-time Frametime Oscilloscope** with 16.6 ms (60 FPS) and 33.3 ms (30 FPS) reference lines.
  - **3 Presets (F8)**: Compact pill, Standard (with oscilloscope), and Detailed (expanded telemetry).
  - **OSD Toast Banners**: On-screen visual feedback for overlay toggles, preset changes, and benchmark states.
- **Hardware Telemetry**:
  - **AMD GPUs**: Linux DRM and hwmon (power, edge/hotspot/memory temperatures, core/memory clocks, VRAM usage, fan RPM, voltage).
  - **NVIDIA GPUs**: NVML dynamic runtime integration (power, temperatures, clocks, VRAM usage, GPU utilization).
  - **Intel Arc / iGPU**: DRM sysfs and hwmon sensors.
  - **CPUs**: sysfs RAPL power caps, coretemp/k10temp temperatures, per-core utilization, and cpufreq clock speeds.
- **Global Hotkeys** (Works across Wayland, Gamescope, and fullscreen games without window focus):
  - **F8**: Cycle In-Game Overlay Presets (Compact → Standard → Detailed).
  - **F9**: Toggle In-Game Overlay HUD.
  - **F10**: Start / Stop benchmark CSV recording.
- **Lossless CSV Benchmarking**: Fast, lossless capture of every single frame event with full telemetry data.
- **Desktop GUI (`gnumon-gui`)**:
  - Live historical multi-metric graph analyzer with dual Y-axes, hover tooltips, and time window selection (2s to 60s).
  - Full PresentMon 2.x Metrics Dictionary inspector (80+ parameters).
  - Floating desktop overlay and Mini-HUD.

## Installation & Packages

### 1. AppImage (Standalone / Portable)
Works out-of-the-box on any modern Linux distribution without installation:
```bash
chmod +x gnumon-0.1.0-x86_64.AppImage
./gnumon-0.1.0-x86_64.AppImage
```

### 2. Debian / Ubuntu (.deb)
```bash
sudo dpkg -i gnumon-0.1.0-Linux.deb
```

### 3. Tarball Archive (.tar.gz)
```bash
tar -xzf gnumon-0.1.0-Linux.tar.gz
cd gnumon-0.1.0-Linux
./scripts/install-layers.sh
```
`install-layers.sh` registers the Vulkan layer for both native Steam and Flatpak Steam.

## Running Games

### Steam Launch Options
In game Properties → Launch Options, set:
```text
gnumon-run %command%
```
Or via environment variable:
```text
ENABLE_GNUMON=1 %command%
```

### Standalone Games / Proton / Lutris
```bash
gnumon-run /path/to/game_executable
```

## Building from Source

### Dependencies
- CMake 3.20+
- Ninja build system
- C++20 compiler (GCC 12+ or Clang 15+)
- Vulkan headers and loader (`vulkan-headers`, `vulkan-icd-loader`)
- Qt6 (`qt6-base`, `libgl-dev`) for `gnumon-gui`

### Host Build
```bash
cmake -B build-host -S . -G Ninja -DCMAKE_BUILD_TYPE=Release
ninja -C build-host
```

### Container Build (Ubuntu 22.04 LTS / GLIBC 2.35)
To build portable binaries against GLIBC 2.35 using Podman/Docker:
```bash
./build-in-container.sh
```

### Building AppImage & Packages
```bash
./packaging/build-appimage.sh
podman run --rm -v $(pwd):/workspace:Z -w /workspace/build-container gnumon-builder:ubuntu22.04 cpack -G "DEB;TGZ"
```

## Architecture

- `src/layer/`: Vulkan layer (`libVkLayer_gnumon.so`), swapchain renderer, and OpenGL wrapper (`libgnumon_gl.so`).
- `src/ipc/`: Lock-free circular ring buffer in POSIX shared memory (`/dev/shm/gnumon_ring_<PID>`).
- `src/control/`: Hardware telemetry providers (AMD DRM, NVIDIA NVML, Intel DRM, Linux CPU) and evdev latency tracker.
- `src/service/`: Telemetry coordinator and sliding statistics engine.
- `src/api/`: Intel PresentMon 2.x C API compatibility layer (`libpresentmon.so`).
- `src/gui/`: Qt6 management application, live graph analyzer, and floating desktop HUD.
- `src/cli/`: Command-line capture tool and telemetry monitor (`gnumon-cli`).
- `src/daemon/`: Background telemetry coordinator service (`gnumond`).

## License

GNU General Public License v3.0 or later (GPL-3.0-or-later).
See `LICENSE` for the complete license text.
