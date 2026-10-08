cmake_minimum_required(VERSION 3.22)
project(gnumon VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Export compile commands for LSP/clangd
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Default to Release if not specified
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# Set RPATH to $ORIGIN so executables can find shared libraries next to them
set(CMAKE_BUILD_WITH_INSTALL_RPATH TRUE)
set(CMAKE_INSTALL_RPATH "$ORIGIN")

# Compiler warnings
add_compile_options(-Wall -Wextra -Wpedantic -Wno-unused-parameter)

# Include directories
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# 1. Common utilities library
add_library(gnumon_common STATIC
    src/common/Clock.cpp
)
target_include_directories(gnumon_common PUBLIC
    include
    src/common
)

# 2. Hardware Control / Telemetry library
add_library(gnumon_control STATIC
    src/control/AmdGpuTelemetry.cpp
    src/control/NvmlGpuTelemetry.cpp
    src/control/IntelGpuTelemetry.cpp
    src/control/CpuTelemetry.cpp
    src/control/InputLatencyTracker.cpp
)
target_link_libraries(gnumon_control PUBLIC
    gnumon_common
    ${CMAKE_DL_LIBS}
)
target_include_directories(gnumon_control PUBLIC
    src/control
)

# 3. Service / Coordinator library
add_library(gnumon_service STATIC
    src/service/TelemetryCoordinator.cpp
)
target_link_libraries(gnumon_service PUBLIC
    gnumon_control
)
target_include_directories(gnumon_service PUBLIC
    src/service
)

# 4. Public PresentMon / Gnumon C API shared library (libpresentmon.so)
add_library(presentmon SHARED
    src/api/PresentMonAPI.cpp
)
target_link_libraries(presentmon PRIVATE
    gnumon_service
)
target_include_directories(presentmon PUBLIC
    include
)
set_target_properties(presentmon PROPERTIES
    OUTPUT_NAME "presentmon"
    SOVERSION 3
    VERSION 3.4.0
)

# 5. Vulkan Capture Layer (libVkLayer_gnumon.so)
find_package(Vulkan QUIET)
if(Vulkan_FOUND OR EXISTS "/usr/include/vulkan/vulkan.h")
    message(STATUS "Vulkan headers found, building VK_LAYER_GNUMON_capture")
    add_library(VkLayer_gnumon SHARED
        src/layer/VkLayer_gnumon.cpp
    )
    target_link_libraries(VkLayer_gnumon PRIVATE
        gnumon_common
    )
    if(TARGET Vulkan::Headers)
        target_link_libraries(VkLayer_gnumon PRIVATE Vulkan::Headers)
    elseif(TARGET Vulkan::Vulkan)
        target_link_libraries(VkLayer_gnumon PRIVATE Vulkan::Vulkan)
    elseif(Vulkan_INCLUDE_DIRS)
        target_include_directories(VkLayer_gnumon PRIVATE ${Vulkan_INCLUDE_DIRS})
    endif()
    target_include_directories(VkLayer_gnumon PRIVATE
        src/ipc
        src/common
    )
    configure_file(
        ${CMAKE_CURRENT_SOURCE_DIR}/src/layer/VkLayer_gnumon.json.in
        ${CMAKE_CURRENT_BINARY_DIR}/VkLayer_gnumon.json
        @ONLY
    )
endif()

# 5b. OpenGL / EGL LD_PRELOAD Capture Layer (libgnumon_gl.so)
add_library(gnumon_gl SHARED
    src/layer/gl_wrapper.cpp
)
target_link_libraries(gnumon_gl PRIVATE
    gnumon_common
    ${CMAKE_DL_LIBS}
)
target_include_directories(gnumon_gl PRIVATE
    src/ipc
    src/common
)

# 6. CLI Tool (gnumon-cli)
add_executable(gnumon-cli
    src/cli/main.cpp
)
target_link_libraries(gnumon-cli PRIVATE
    presentmon
)

# 7. Service Daemon (gnumond)
add_executable(gnumond
    src/daemon/main.cpp
)
target_link_libraries(gnumond PRIVATE
    gnumon_service
    gnumon_common
)

# 7. Optional Qt6 GUI
find_package(Qt6 COMPONENTS Core Gui Widgets QUIET)
if(Qt6_FOUND)
    message(STATUS "Qt6 found (${Qt6_VERSION}), enabling gnumon-gui target")
    set(CMAKE_AUTOMOC ON)
    add_executable(gnumon-gui
        src/gui/main.cpp
        src/gui/MainWindow.cpp
        src/gui/MainWindow.h
        src/gui/FrametimeGraphWidget.cpp
        src/gui/FrametimeGraphWidget.h
        src/gui/MetricsConfigDialog.cpp
        src/gui/MetricsConfigDialog.h
        src/gui/PresentMonOverlay.cpp
        src/gui/PresentMonOverlay.h
    )
    target_link_libraries(gnumon-gui PRIVATE
        Qt6::Core
        Qt6::Gui
        Qt6::Widgets
        presentmon
    )
else()
    message(STATUS "Qt6 not found or disabled, skipping gnumon-gui")
endif()

# --- Installation Rules ---
include(GNUInstallDirs)

install(TARGETS presentmon
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
)
install(TARGETS gnumon-cli gnumond
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
if(TARGET gnumon-gui)
    install(TARGETS gnumon-gui
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    )
endif()
if(TARGET VkLayer_gnumon)
    install(TARGETS VkLayer_gnumon
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    )
    install(FILES ${CMAKE_CURRENT_BINARY_DIR}/VkLayer_gnumon.json
        DESTINATION ${CMAKE_INSTALL_DATADIR}/vulkan/explicit_layer.d
    )
endif()
if(TARGET gnumon_gl)
    install(TARGETS gnumon_gl
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    )
endif()
install(DIRECTORY include/gnumon
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)
install(FILES systemd/gnumond.service
    DESTINATION ${CMAKE_INSTALL_DATADIR}/systemd/user
)
install(PROGRAMS
    scripts/gnumon-run
    scripts/install-layers.sh
    scripts/uninstall-layers.sh
    DESTINATION ${CMAKE_INSTALL_BINDIR}
)
install(FILES README.md LICENSE
    DESTINATION ${CMAKE_INSTALL_DOCDIR}
)

# --- CPack Configuration (.deb, .rpm, .tar.gz) ---
set(CPACK_PACKAGE_NAME "gnumon")
set(CPACK_PACKAGE_VENDOR "gnumon team")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Intel PresentMon port for Linux (Performance & Telemetry)")
set(CPACK_PACKAGE_VERSION "0.1.0")
set(CPACK_PACKAGE_CONTACT "https://github.com/ryslavy/gnumon")
set(CPACK_GENERATOR "TGZ")
set(CPACK_DEBIAN_PACKAGE_MAINTAINER "gnumon team")
set(CPACK_DEBIAN_PACKAGE_SECTION "utils")
set(CPACK_RPM_PACKAGE_LICENSE "GPL-3.0-or-later")
set(CPACK_RPM_PACKAGE_GROUP "Development/Tools")
include(CPack)

message(STATUS "Configured gnumon (Linux PresentMon) successfully.")

