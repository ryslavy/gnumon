cmake_minimum_required(VERSION 3.22)
project(gnumon VERSION 0.2.0 LANGUAGES C CXX)

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

# Set RPATH so executables can find shared libraries next to them or in ../lib
set(CMAKE_BUILD_WITH_INSTALL_RPATH TRUE)
set(CMAKE_INSTALL_RPATH "$ORIGIN;$ORIGIN/../lib;$ORIGIN/../lib/gnumon;$ORIGIN/../lib64")

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
    target_link_options(VkLayer_gnumon PRIVATE -static-libstdc++ -static-libgcc)
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
target_link_options(gnumon_gl PRIVATE -static-libstdc++ -static-libgcc)
target_include_directories(gnumon_gl PRIVATE
    src/ipc
    src/common
)

# 5c. 32-bit Multilib Layers (libVkLayer_gnumon.so & libgnumon_gl.so for 32-bit Proton / Wine games)
include(CheckCXXCompilerFlag)
set(CMAKE_REQUIRED_FLAGS "-m32")
set(CMAKE_REQUIRED_LINK_OPTIONS "-m32")
check_cxx_compiler_flag("-m32" COMPILER_SUPPORTS_M32)
unset(CMAKE_REQUIRED_FLAGS)
unset(CMAKE_REQUIRED_LINK_OPTIONS)

if(COMPILER_SUPPORTS_M32)
    message(STATUS "32-bit multilib supported: enabling 32-bit Vulkan & OpenGL layer targets")

    add_library(gnumon_common_32 STATIC
        src/common/Clock.cpp
    )
    target_compile_options(gnumon_common_32 PRIVATE -m32 -fPIC)
    target_include_directories(gnumon_common_32 PUBLIC
        include
        src/common
    )

    if(TARGET VkLayer_gnumon)
        add_library(VkLayer_gnumon_32 SHARED
            src/layer/VkLayer_gnumon.cpp
        )
        target_compile_options(VkLayer_gnumon_32 PRIVATE -m32 -fPIC)
        target_link_options(VkLayer_gnumon_32 PRIVATE -m32 -static-libstdc++ -static-libgcc)
        target_link_libraries(VkLayer_gnumon_32 PRIVATE
            gnumon_common_32
        )
        if(Vulkan_INCLUDE_DIRS)
            target_include_directories(VkLayer_gnumon_32 PRIVATE ${Vulkan_INCLUDE_DIRS})
        endif()
        target_include_directories(VkLayer_gnumon_32 PRIVATE
            include
            src/ipc
            src/common
        )
        set_target_properties(VkLayer_gnumon_32 PROPERTIES
            OUTPUT_NAME "VkLayer_gnumon"
            LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/lib32"
        )
    endif()

    add_library(gnumon_gl_32 SHARED
        src/layer/gl_wrapper.cpp
    )
    target_compile_options(gnumon_gl_32 PRIVATE -m32 -fPIC)
    target_link_options(gnumon_gl_32 PRIVATE -m32 -static-libstdc++ -static-libgcc)
    target_link_libraries(gnumon_gl_32 PRIVATE
        gnumon_common_32
        ${CMAKE_DL_LIBS}
    )
    target_include_directories(gnumon_gl_32 PRIVATE
        include
        src/ipc
        src/common
    )
    set_target_properties(gnumon_gl_32 PROPERTIES
        OUTPUT_NAME "gnumon_gl"
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/lib32"
    )
endif()

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
        src/gui/MainViewWidget.cpp
        src/gui/MainViewWidget.h
        src/gui/SettingsPage.cpp
        src/gui/SettingsPage.h
        src/gui/LoadoutConfigPage.cpp
        src/gui/LoadoutConfigPage.h
        src/gui/LoadoutRowWidget.cpp
        src/gui/LoadoutRowWidget.h
        src/gui/WindowedOverlayWidget.cpp
        src/gui/WindowedOverlayWidget.h
        src/gui/ToggleSwitch.cpp
        src/gui/ToggleSwitch.h
        src/gui/HotkeyPillWidget.cpp
        src/gui/HotkeyPillWidget.h
        src/gui/QuadrantPositioner.cpp
        src/gui/QuadrantPositioner.h
        src/gui/ColorPickerButton.cpp
        src/gui/ColorPickerButton.h
        src/gui/AppConfig.h
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
        DESTINATION ${CMAKE_INSTALL_DATADIR}/vulkan/implicit_layer.d
    )
endif()
if(TARGET gnumon_gl)
    install(TARGETS gnumon_gl
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    )
endif()
if(TARGET VkLayer_gnumon_32)
    install(TARGETS VkLayer_gnumon_32
        LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}32"
    )
    install(TARGETS gnumon_gl_32
        LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}32"
    )
endif()
install(DIRECTORY include/gnumon
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)
install(FILES systemd/gnumond.service
    DESTINATION ${CMAKE_INSTALL_DATADIR}/systemd/user
)
install(FILES scripts/99-gnumon-input.rules
    DESTINATION lib/udev/rules.d
)
install(PROGRAMS
    scripts/gnumon-run
    scripts/setup-service.sh
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
set(CPACK_DEBIAN_PACKAGE_ARCHITECTURE "amd64")
set(CPACK_RPM_PACKAGE_LICENSE "GPL-3.0-or-later")
set(CPACK_RPM_PACKAGE_GROUP "Development/Tools")
include(CPack)

message(STATUS "Configured gnumon (Linux PresentMon) successfully.")

