# Contributing & Issue Reporting Guide

Thank you for your interest in improving **gnumon**! This guide outlines how to report issues effectively, collect relevant diagnostic logs, and contribute improvements to the project.

---

## 1. Reporting Issues

Before opening a new issue, please search the [Existing Issues](https://github.com/ryslavy/gnumon/issues) to verify if the problem has already been reported.

When reporting a bug, use the **Bug Report** template and provide the following essential details:

### A. System & Environment
- **Linux Distribution**: (e.g. CachyOS, Arch Linux, Fedora 40, Ubuntu 24.04)
- **Kernel Version**: Run `uname -r`
- **Display Server & Desktop**: Wayland or X11; KDE Plasma, GNOME, Sway, Hyprland, Gamescope, etc.
- **GPU & Driver**: Run `inxi -G` or `vulkaninfo --summary`
- **gnumon Version**: AppImage, package, or commit hash

### B. Collecting Diagnostic Logs

#### 1. Enable In-Game Debug Logging
Run the target game or application from a terminal (or set in Steam Launch Options) with the `GNUMON_DEBUG=1` environment variable:
```bash
GNUMON_DEBUG=1 %command%
```
or for standalone executables:
```bash
GNUMON_DEBUG=1 vkcube
```
This prints layer hook initialization, swapchain detection, hotkey events, and frame pacing diagnostics to `stderr`.

#### 2. Inspecting the Background Service (`gnumond`)
If hardware telemetry or global hotkeys fail to register, inspect the systemd daemon logs:
```bash
journalctl --user -u gnumond -n 100 --no-pager
```
Or run `gnumond` in the foreground with verbose logging:
```bash
systemctl --user stop gnumond
gnumond -f
```

#### 3. Verifying Layer Registration
To ensure the Vulkan implicit layer is detected by the Vulkan loader:
```bash
vulkaninfo | grep -i gnumon
```
You should see:
```text
VK_LAYER_GNUMON_capture (gnumon performance monitoring layer)
```

---

## 2. Feature Requests

If you have an idea for a new metric, hardware sensor, overlay widget, or UX enhancement:
1. Open a **Feature Request** issue describing the use case and proposed workflow.
2. If it relates to Intel PresentMon parity, reference the corresponding metric name or UI component from PresentMon 2.x.

---

## 3. Development & Building from Source

### Quick Build (Host)
```bash
cmake -B build-host -S . -G Ninja -DCMAKE_BUILD_TYPE=Release
ninja -C build-host
```

### Reproducible Multilib Container Build (Ubuntu 22.04 LTS / glibc 2.32)
```bash
./build-in-container.sh
```

### Generating Standalone AppImage
```bash
./packaging/build-appimage.sh
```

### Installing Layers to Local System
```bash
./scripts/install-layers.sh
```
