# gnumon

Linux-native port of Intel PresentMon providing real-time frame timing, GPU/CPU hardware telemetry, an in-game overlay, and benchmark CSV capture for Vulkan and OpenGL applications.

## Overview

gnumon brings Intel PresentMon metrics to Linux with 100% C API compatibility (`PresentMonAPI.h`). It captures presentation timestamps directly from graphics APIs and provides low-overhead IPC, sub-thread TID resolution, hardware sensors monitoring (AMD, NVIDIA, Intel), and an Intel PresentMon 2.x style in-game HUD.

## Features

- Frame Timing: Displayed FPS, presented FPS, CPU frame time, GPU busy time, GPU wait time, display latency, and dropped frames.
- Graphics APIs: Vulkan implicit and explicit layer (`VK_LAYER_GNUMON_capture`), OpenGL/EGL swap wrapper (`libgnumon_gl.so`).
- Input Latency: Mouse click-to-photon latency tracking via Linux evdev (`/dev/input/event*`).
- Telemetry:
  - AMD GPUs: DRM and hwmon (power, edge/junction/VRAM temperatures, clocks, VRAM usage, fan RPM, voltage).
  - NVIDIA GPUs: NVML runtime dynamic loader (power, temperatures, clocks, VRAM usage, utilization).
  - Intel Arc / iGPU: DRM sysfs and hwmon.
  - CPUs: sysfs RAPL power caps, coretemp/k10temp temperatures, cpufreq clock speeds, and /proc/stat utilization.
- In-Game Overlay: Real-time HUD styled after Intel PresentMon 2.x with customizable corners, dragging, and live frametime/GPU busy graph.
- Global Hotkeys:
  - F11: Toggle overlay visibility.
  - F10: Start / stop benchmark CSV capture.
- CLI and GUI:
  - `gnumon-gui`: Qt6 desktop application and overlay controller.
  - `gnumon-cli`: Command-line capture tool and live console telemetry.
  - `gnumond`: Background telemetry coordinator service.

## Quick Start (Pre-built Package)

1. Extract the release archive:
```bash
tar -xzf gnumon-0.1.0-Linux.tar.gz
cd gnumon-0.1.0-Linux
```

2. Run the layer installation script:
```bash
./scripts/install-layers.sh
```
This registers the Vulkan layer into `~/.local/share/vulkan/` and copies binaries to `~/.local/bin` without requiring root permissions.

3. Launch the GUI:
```bash
gnumon-gui
```

## Running Games

### Steam Launch Options
Set the launch options of your game in Steam:
```text
gnumon-run %command%
```
Or via environment variable:
```text
ENABLE_GNUMON=1 %command%
```

### Standalone Games / Proton
```bash
gnumon-run /path/to/game_executable
```

## Building from Source

### Dependencies
- CMake 3.20+
- Ninja build system
- C++20 compliant compiler (GCC 12+ or Clang 15+)
- Vulkan headers and loader (`vulkan-headers`, `vulkan-icd-loader`)
- Qt6 (Optional, for `gnumon-gui`: `qt6-base`)

### Build Steps
```bash
cmake -B build -S . -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
```

To create a release tarball:
```bash
cpack --config build/CPackConfig.cmake
```

### Podman / Docker Container Build
To build in a clean Ubuntu 22.04 LTS container environment:
```bash
./build-in-container.sh
```

## Architecture

- `src/layer/`: Vulkan layer (`libVkLayer_gnumon.so`) and OpenGL wrapper (`libgnumon_gl.so`).
- `src/ipc/`: Lock-free SPSC circular ring buffer residing in POSIX shared memory (`/dev/shm/gnumon_ring_<PID>`).
- `src/control/`: Hardware telemetry providers (AMD DRM, NVIDIA NVML, Intel DRM, Linux CPU) and evdev latency tracker.
- `src/service/`: Central telemetry coordinator (`gnumond`).
- `src/api/`: Intel PresentMon 2.x C API compatibility layer (`libpresentmon.so`).
- `src/gui/`: Qt6 management application and floating in-game overlay.
- `src/cli/`: Command-line interface (`gnumon-cli`).

## License

GNU General Public License v3.0 or later (GPL-3.0-or-later).
See `LICENSE` for the complete license text.
