# gnumon

Linux-native port of Intel PresentMon providing real-time frame timing, GPU/CPU hardware telemetry, an in-game swapchain overlay with a frametime oscilloscope, and benchmark CSV capture for Vulkan and OpenGL applications.

## Overview

**gnumon** brings Intel PresentMon 2.x metrics and workflows to Linux with full C API compatibility (`PresentMonAPI.h`). It captures presentation timestamps directly from graphics APIs via a high-performance Vulkan layer and provides lock-free IPC, hardware telemetry (AMD, NVIDIA, Intel), an in-game HUD with a real-time frametime oscilloscope, and a Qt6 GUI with live graph analytics.

## Features

- **Frametime & Pacing Analysis**:
  - Displayed FPS, Presented FPS, and Application FPS (distinguishing native application frames from FSR 3 / DLSS 3 frame generation).
  - CPU frame time, GPU busy time, GPU wait time, render-to-display latency, and animation error.
  - Real-time **Pipeline State Object (PSO) & Shader Compilation Tracking** (`vkCreateGraphicsPipelines`, `vkCreateComputePipelines`) to detect stutter and shader compilation spikes.
  - 1% Low and 0.1% Low framerates.
- **In-Game Swapchain Overlay & 32-Bit Multilib Support**:
  - Injected directly into the game's swapchain with zero compositor latency.
  - **Full 64-bit & 32-bit Multilib Hooking**: Seamlessly captures both 64-bit and legacy 32-bit Proton / Wine games (`VkLayer_gnumon.so`, `VkLayer_gnumon_32.so`, OpenGL wrappers).
  - **Real-time Frametime Oscilloscope** with 16.6 ms (60 FPS) and 33.3 ms (30 FPS) reference lines.
  - **3 Presets (F8)**: Compact pill, Standard (with oscilloscope), and Detailed (expanded telemetry).
  - **OSD Toast Banners**: On-screen visual feedback for overlay toggles, preset changes, and benchmark states.
- **Hardware Telemetry & Dynamic Power Limits**:
  - **AMD GPUs**: Linux DRM and hwmon (power, hardware power limit `power1_cap`, edge/hotspot/memory temperatures, core/memory clocks, VRAM usage, fan RPM, voltage).
  - **NVIDIA GPUs**: NVML dynamic runtime integration (power, dynamic power management limits, temperatures, clocks, VRAM usage, GPU utilization).
  - **Intel Arc / iGPU**: DRM sysfs and hwmon sensors.
  - **CPUs**: sysfs RAPL power caps (`constraint_0_power_limit_uj`), coretemp/k10temp temperatures, per-core utilization, and cpufreq clock speeds.
- **1:1 Intel PresentMon Windows UI Parity (Single-Window, Zero Popups)**:
  - Exact layout, typography, and card-based workflow faithful to Intel PresentMon 2.x (Process Tracking, Presets [BASIC, GAME EXPERIENCE, GPU FOCUS, POWER/TEMP, CUSTOM], Capture Duration, Capture Hotkey, and Capture Storage).
  - **Loadout Configuration Editor**: Full widget loadout editor with reorderable metric rows, readout/graph display modes, stat selection (avg, 99%, min, max), color swatches, and JSON save/load (`p2c-cap-load` 1.0.0 format).
  - **Integrated Settings Navigation**: Left drawer navigation (`< TOP`, `Overlay`, `Data`, `Capture`, `Logging`, `Other`, `About`) directly within the main window with zero popup dialogs.
  - **Built-in Linux System & Service Integration**: Seamlessly embedded in the "Other" page — 1-click systemd daemon (`gnumond`) control, polkit/pkexec udev input permissions for Click-to-Photon latency and Wayland hotkeys, and 64/32-bit Vulkan layers installation.
  - **Deep Blue Status Bar**: Real-time display of tracked process, recording state (`● REC`), Autohide status, polling frequency, and overlay draw rate.
- **Global Hotkeys** (Works across Wayland, Gamescope, and fullscreen games without window focus):
  - **F8 / F11 / Ctrl+Shift+P**: Cycle Presets.
  - **F9 / Ctrl+Shift+O**: Toggle In-Game Overlay HUD.
  - **F10 / Ctrl+Shift+K**: Start / Stop benchmark CSV recording.
- **Lossless CSV Benchmarking**: Fast, lossless capture of every single frame event with full hardware telemetry data.

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
