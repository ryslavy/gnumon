# gnumon — Linux PresentMon Feature Parity Checklist

Kompletní seznam funkcí a metrik podle referenčního Intel PresentMon 2.6.0 pro dosažení 100% parity na Linuxu.

---

## 1. Základní architektura, Build & Kompatibilita
- [x] Samostatný build strom na bázi CMake a Ninja
- [x] Podman kontejnerové prostředí pro zpětnou kompatibilitu (`GLIBC_2.32`, Ubuntu 22.04 LTS)
- [x] Skript pro automatické sestavení v kontejneru (`build-in-container.sh`)
- [x] Nanosekundová časová základna `CLOCK_MONOTONIC_RAW` (emulace Windows QPC)
- [x] Bezzámkový SPSC kruhový buffer v POSIX shared memory (`/dev/shm/gnumon_ring_<PID>`)
- [x] Samostatný systémový/uživatelský démon `gnumond` (Unix Domain Socket IPC)
- [x] Podpora pro balíčky (CPack TGZ/DEB/RPM, Arch PKGBUILD)

---

## 2. Snímkové metriky a prezentace (Pipeline Timing)
- [x] Vulkan Explicit Layer (`VK_LAYER_GNUMON_capture`)
- [x] Záchyt volání `vkQueuePresentKHR` (čas zavolání, délka trvání prezentace)
- [x] Záchyt začátku CPU snímku v `vkAcquireNextImageKHR` / `vkAcquireNextImage2KHR`
- [x] Výpočet `PM_METRIC_CPU_FRAME_TIME`, `PM_METRIC_DISPLAYED_FRAME_TIME`, `PM_METRIC_PRESENTED_FRAME_TIME`
- [x] Výpočet `PM_METRIC_DISPLAYED_FPS`, `PM_METRIC_PRESENTED_FPS`, `PM_METRIC_APPLICATION_FPS`
- [x] Identifikace `PM_METRIC_SWAP_CHAIN_ADDRESS` a `PM_METRIC_PRESENT_RUNTIME` (Vulkan)
- [x] Měření aktivní práce GPU přes `vkQueueSubmit` / `vkQueueSubmit2` (`PM_METRIC_GPU_TIME`, `PM_METRIC_GPU_BUSY`, `PM_METRIC_GPU_WAIT`)
- [x] Výpočet latence prezentace a zobrazení (`PM_METRIC_UNTIL_DISPLAYED`, `PM_METRIC_DISPLAY_LATENCY`)
- [x] Detekce zahozených snímků (`PM_METRIC_DROPPED_FRAMES`)
- [x] Záchyt latence vstupu z `/dev/input/event*` (`PM_METRIC_CLICK_TO_PHOTON_LATENCY`)
- [x] OpenGL podpora (`glXSwapBuffers` / `eglSwapBuffers` LD_PRELOAD wrapper: `libgnumon_gl.so`)
- [x] Detekce Frame Generation (AMD AFMF / FSR3, Intel XeFG)

---

## 3. Hardwarová telemetrie GPU
- [x] **AMD GPU (Linux DRM / sysfs hwmon):**
  - [x] Příkon ve wattech (`PM_METRIC_GPU_POWER`)
  - [x] Teploty: jádro / edge, hotspot / junction, VRAM (`PM_METRIC_GPU_TEMPERATURE`)
  - [x] Využití jádra a paměti v % (`PM_METRIC_GPU_UTILIZATION`)
  - [x] Takty jádra a VRAM v MHz (`PM_METRIC_GPU_FREQUENCY`)
  - [x] Využití a celková velikost VRAM v bytech (`PM_METRIC_GPU_MEM_USED`, `PM_METRIC_GPU_MEM_SIZE`)
  - [x] Otáčky ventilátoru v RPM (`PM_METRIC_GPU_FAN_SPEED`)
  - [x] Napětí jádra v mV (`PM_METRIC_GPU_VOLTAGE`)
- [x] **NVIDIA GPU (NVML):**
  - [x] Implementace `NvmlGpuTelemetry` pro Linux `libnvidia-ml.so` (dynamický dlopen)
  - [x] Teploty, spotřeba, takty, VRAM, vytížení
- [x] **Intel Arc / iGPU:**
  - [x] Telemetrie přes Intel DRM sysfs a hwmon (`IntelGpuTelemetry`)
- [x] **GPU Limity & Throttling:**
  - [x] Detekce omezení příkonem, teplotou nebo napětím (`PM_METRIC_GPU_POWER_LIMITED`, `PM_METRIC_GPU_TEMPERATURE_LIMITED`)

---

## 4. Hardwarová telemetrie CPU
- [x] Celkové vytížení procesoru v % (`PM_METRIC_CPU_UTILIZATION` z `/proc/stat`)
- [x] Příkon CPU balíčku ve wattech (`PM_METRIC_CPU_POWER` přes RAPL `powercap`)
- [x] Průměrná teplota CPU (`PM_METRIC_CPU_TEMPERATURE` z `coretemp` / `k10temp`)
- [x] Frekvence procesoru v MHz (`PM_METRIC_CPU_FREQUENCY` z `cpufreq`)
- [x] Per-core vytížení procesoru (`PM_METRIC_CPU_CORE_UTILITY`)
- [x] Per-core teploty jader (`PM_METRIC_CPU_CORE_TEMPERATURE`)
- [x] Parsování názvu a výrobce CPU z `/proc/cpuinfo` (`PM_METRIC_CPU_NAME`, `PM_METRIC_CPU_VENDOR`)

---

## 5. Klientské C API (`PresentMonAPI.h`) a Middleware
- [x] `pmGetApiVersion`
- [x] `pmOpenSession` / `pmCloseSession`
- [x] `pmStartTrackingProcess` / `pmStopTrackingProcess`
- [x] `pmRegisterDynamicQuery` / `pmFreeDynamicQuery` / `pmPollDynamicQuery`
- [x] Streamování snímků `pmRegisterFrameQuery` / `pmConsumeFrames` / `pmFreeFrameQuery`
- [x] Statistické agregace v `pmPollDynamicQuery` (AVG, 1% Low / 99th percentile, 0.1% Low, MIN, MAX)
- [x] Introspekce metrik `pmGetIntrospectionRoot` / `pmFreeIntrospectionRoot`
- [x] C++ OOP knihovní wrapper (`PresentMonAPIWrapper.hpp`)

---

## 6. Konzolová aplikace (`gnumon-cli`)
- [x] Živý terminálový výpis telemetrie a FPS
- [x] Sledování podle PID (`./gnumon-cli <PID>`)
- [x] Vyhledání procesu podle názvu (`--process_name <name>`)
- [x] Záznam per-frame metrik do CSV souboru (`--output_file <path>`)
- [x] Streamování CSV na standardní výstup (`--output_stdout`)
- [x] Časované nahrávání (`--delay <sec>`, `--timed <sec>`)
- [x] Globální klávesová zkratka pro start/stop záznamu (`--hotkey <key>`)
- [x] Shoda formátu CSV hlaviček s PresentMon 2.x

---

## 7. Grafické rozhraní (Qt GUI) a In-Game Overlay
- [x] Nativní Qt6 okno (`gnumon-gui`)
- [x] Živé ukazatele GPU a CPU telemetrie
- [x] Ovládání záznamu (tlačítko Start/Stop)
- [x] Plynulý graf historie snímkových časů (Frametime graph v reálném čase)
- [x] Výběr sledovaného procesu z běžících aplikací (`comboProcess_` ze skenu `/proc`)
- [x] Nahrávání a ukládání CSV přímo z GUI
- [x] In-Game Overlay (zobrazení statistik přímo ve hře přes Vulkan swapchain)
- [x] In-Game osciloskop snímkových časů (frametime oscilloscope) s referenčními linkami 60 FPS / 33 FPS
- [x] 3 přepínatelné presety in-game overlaye (F8: Compact, Standard s osciloskopem, Detailed) a OSD toasty
- [x] 32-bit multilib podpora (`lib32/libVkLayer_gnumon.so`, `lib32/libgnumon_gl.so`) pro 32-bit Proton/Wine hry
- [x] Sledování kompilace shaderů a PSO v reálném čase (`vkCreateGraphicsPipelines`, `vkCreateComputePipelines`, `PM_METRIC_PSO_COMPILE_COUNT`, `PM_METRIC_PSO_COMPILE_TIME`, `PM_METRIC_PSO_COMPILE_BUSY_PERCENT`)
- [x] Dynamické hardwarové limity příkonu (AMD hwmon `power1_cap`, NVIDIA NVML limits, CPU RAPL constraints)
- [x] LACT-style Service & Setup dialog (`ServiceSetupDialog` v GUI, `setup-service.sh`, `99-gnumon-input.rules`) pro správu daemona a uaccess oprávnění pro měření latence myši / kláves bez nutnosti rootu
- [x] Konfigurační dialog pro výběr sledovaných metrik
- [x] All PresentMon Metrics Inspector okno (80+ metrik, grafy, limity)
