#!/usr/bin/env bash
# build-appimage.sh — Automated AppImage packager for gnumon (Linux PresentMon)
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
APPDIR="${SCRIPT_DIR}/build-appimage/AppDir"
OUT_DIR="${SCRIPT_DIR}/build-appimage"
APPIMAGETOOL="/tmp/appimagetool.AppImage"

echo "==> Preparing gnumon AppImage build..."

# 1. Download appimagetool if not available
if [ ! -f "$APPIMAGETOOL" ]; then
    echo "==> Downloading appimagetool..."
    curl -s -L -o "$APPIMAGETOOL" https://github.com/AppImage/appimagetool/releases/download/continuous/appimagetool-x86_64.AppImage
    chmod +x "$APPIMAGETOOL"
fi

# 2. Clean and create AppDir structure
rm -rf "$APPDIR"
mkdir -p "$APPDIR"/usr/{bin,lib,lib32,plugins,share/icons/hicolor/scalable/apps,share/applications}

# 3. Copy binaries & scripts
echo "==> Copying binaries..."
cp -f "${SCRIPT_DIR}/build-host/gnumon-gui" "$APPDIR/usr/bin/"
cp -f "${SCRIPT_DIR}/build-host/gnumon-cli" "$APPDIR/usr/bin/"
cp -f "${SCRIPT_DIR}/build-host/gnumond" "$APPDIR/usr/bin/"
cp -f "${SCRIPT_DIR}/scripts/gnumon-run" "$APPDIR/usr/bin/"
cp -f "${SCRIPT_DIR}/scripts/setup-service.sh" "$APPDIR/usr/bin/"
cp -f "${SCRIPT_DIR}/scripts/install-layers.sh" "$APPDIR/usr/bin/"
cp -f "${SCRIPT_DIR}/scripts/uninstall-layers.sh" "$APPDIR/usr/bin/"
mkdir -p "$APPDIR/usr/lib/udev/rules.d"
cp -f "${SCRIPT_DIR}/scripts/99-gnumon-input.rules" "$APPDIR/usr/lib/udev/rules.d/"

# 4. Copy libraries
echo "==> Copying gnumon libraries..."
cp -a "${SCRIPT_DIR}/build-host"/libpresentmon.so* "$APPDIR/usr/lib/"
if [ -f "${SCRIPT_DIR}/build-container/libVkLayer_gnumon.so" ]; then
    cp -f "${SCRIPT_DIR}/build-container/libVkLayer_gnumon.so" "$APPDIR/usr/lib/"
    cp -f "${SCRIPT_DIR}/build-container/libgnumon_gl.so" "$APPDIR/usr/lib/"
else
    cp -f "${SCRIPT_DIR}/build-host/libVkLayer_gnumon.so" "$APPDIR/usr/lib/"
    cp -f "${SCRIPT_DIR}/build-host/libgnumon_gl.so" "$APPDIR/usr/lib/"
fi

# 32-bit Multilib libraries
if [ -f "${SCRIPT_DIR}/build-container/lib32/libVkLayer_gnumon.so" ]; then
    cp -f "${SCRIPT_DIR}/build-container/lib32/libVkLayer_gnumon.so" "$APPDIR/usr/lib32/"
    cp -f "${SCRIPT_DIR}/build-container/lib32/libgnumon_gl.so" "$APPDIR/usr/lib32/"
elif [ -f "${SCRIPT_DIR}/build-host/lib32/libVkLayer_gnumon.so" ]; then
    cp -f "${SCRIPT_DIR}/build-host/lib32/libVkLayer_gnumon.so" "$APPDIR/usr/lib32/"
    cp -f "${SCRIPT_DIR}/build-host/lib32/libgnumon_gl.so" "$APPDIR/usr/lib32/"
fi

# 5. Copy Qt6 plugins
echo "==> Copying Qt6 plugins..."
QT_PLUGINS_SRC="/usr/lib/qt6/plugins"
mkdir -p "$APPDIR/usr/plugins"/{platforms,wayland-shell-integration,wayland-graphics-integration-client,wayland-decoration-client,xcbglintegrations,imageformats,styles}

cp -f "$QT_PLUGINS_SRC"/platforms/libqxcb.so "$APPDIR/usr/plugins/platforms/"
if [ -f "$QT_PLUGINS_SRC"/platforms/libqwayland.so ]; then
    cp -f "$QT_PLUGINS_SRC"/platforms/libqwayland.so "$APPDIR/usr/plugins/platforms/"
fi

if [ -d "$QT_PLUGINS_SRC"/wayland-shell-integration ]; then
    cp -a "$QT_PLUGINS_SRC"/wayland-shell-integration/* "$APPDIR/usr/plugins/wayland-shell-integration/" 2>/dev/null || true
fi
if [ -d "$QT_PLUGINS_SRC"/wayland-graphics-integration-client ]; then
    cp -a "$QT_PLUGINS_SRC"/wayland-graphics-integration-client/* "$APPDIR/usr/plugins/wayland-graphics-integration-client/" 2>/dev/null || true
fi
if [ -d "$QT_PLUGINS_SRC"/wayland-decoration-client ]; then
    cp -a "$QT_PLUGINS_SRC"/wayland-decoration-client/* "$APPDIR/usr/plugins/wayland-decoration-client/" 2>/dev/null || true
fi
if [ -d "$QT_PLUGINS_SRC"/xcbglintegrations ]; then
    cp -a "$QT_PLUGINS_SRC"/xcbglintegrations/* "$APPDIR/usr/plugins/xcbglintegrations/" 2>/dev/null || true
fi
if [ -d "$QT_PLUGINS_SRC"/styles ]; then
    cp -a "$QT_PLUGINS_SRC"/styles/* "$APPDIR/usr/plugins/styles/" 2>/dev/null || true
fi

# Copy core image formats only
for fmt in libqsvg.so libqpng.so libqjpeg.so libqico.so; do
    if [ -f "$QT_PLUGINS_SRC/imageformats/$fmt" ]; then
        cp -f "$QT_PLUGINS_SRC/imageformats/$fmt" "$APPDIR/usr/plugins/imageformats/"
    fi
done

# 6. Collect Qt6 & other dynamic shared library dependencies
echo "==> Resolving and bundling shared libraries..."
python3 - <<PYEOF
import os, subprocess, shutil

dest_lib = "$APPDIR/usr/lib"
targets = ["$APPDIR/usr/bin/gnumon-gui"]
for root, dirs, files in os.walk("$APPDIR/usr/plugins"):
    for f in files:
        if f.endswith(".so"):
            targets.append(os.path.join(root, f))

# System libraries that must come from the host OS
exclude_prefixes = (
    "libc.so", "libm.so", "libpthread.so", "libdl.so", "librt.so", "ld-linux",
    "libGL.so", "libGLX.so", "libEGL.so", "libGLdispatch.so", "libdrm.so",
    "libasound.so"
)

collected = set()
to_scan = list(targets)

while to_scan:
    item = to_scan.pop()
    if not os.path.exists(item): continue
    try:
        out = subprocess.check_output(["ldd", item], text=True)
    except Exception:
        continue
    for line in out.splitlines():
        if "=>" in line:
            parts = line.split("=>")
            libname = parts[0].strip()
            rest = parts[1].strip().split()[0]
            if os.path.exists(rest) and not any(libname.startswith(p) for p in exclude_prefixes):
                if rest not in collected and "/usr/lib" in rest:
                    collected.add(rest)
                    to_scan.append(rest)

for lib in sorted(collected):
    target_path = os.path.join(dest_lib, os.path.basename(lib))
    if not os.path.exists(target_path):
        shutil.copy2(lib, target_path)
print(f"Bundled {len(collected)} dynamic libraries.")
PYEOF

# Clean any accidentally copied host ld-linux
rm -f "$APPDIR"/usr/lib/ld-linux*

# 7. Desktop entry and icons
echo "==> Setting up metadata and desktop integration..."
cp -f "${SCRIPT_DIR}/packaging/gnumon.desktop" "$APPDIR/"
cp -f "${SCRIPT_DIR}/packaging/gnumon.desktop" "$APPDIR/usr/share/applications/"
cp -f "${SCRIPT_DIR}/packaging/gnumon.svg" "$APPDIR/"
cp -f "${SCRIPT_DIR}/packaging/gnumon.svg" "$APPDIR/usr/share/icons/hicolor/scalable/apps/gnumon.svg"

# 8. Create AppRun entrypoint
cat <<'APPRUN_EOF' > "$APPDIR/AppRun"
#!/bin/sh
set -e

APPDIR="$(dirname "$(readlink -f "${0}")")"

export PATH="${APPDIR}/usr/bin:${PATH}"
export LD_LIBRARY_PATH="${APPDIR}/usr/lib:${LD_LIBRARY_PATH}"
export QT_PLUGIN_PATH="${APPDIR}/usr/plugins"
export XDG_DATA_DIRS="${APPDIR}/usr/share:${XDG_DATA_DIRS:-/usr/local/share:/usr/share}"

case "${1:-}" in
    setup-service|--setup-service)
        shift
        exec "${APPDIR}/usr/bin/setup-service.sh" "$@"
        ;;
    install-layer|--install-layer)
        shift
        exec "${APPDIR}/usr/bin/install-layers.sh" "$@"
        ;;
    uninstall-layer|--uninstall-layer)
        shift
        exec "${APPDIR}/usr/bin/uninstall-layers.sh" "$@"
        ;;
esac

# If first arg is a known tool, run it; otherwise launch GUI
if [ $# -gt 0 ] && [ -x "${APPDIR}/usr/bin/$1" ]; then
    CMD="${APPDIR}/usr/bin/$1"
    shift
    exec "$CMD" "$@"
else
    exec "${APPDIR}/usr/bin/gnumon-gui" "$@"
fi
APPRUN_EOF
chmod +x "$APPDIR/AppRun"

# 9. Build AppImage using appimagetool
APPIMAGE_OUTPUT="${OUT_DIR}/gnumon-0.1.0-x86_64.AppImage"
echo "==> Generating AppImage: ${APPIMAGE_OUTPUT}..."
ARCH=x86_64 "$APPIMAGETOOL" "$APPDIR" "$APPIMAGE_OUTPUT"

echo "==> Successfully created ${APPIMAGE_OUTPUT}"
ls -lh "$APPIMAGE_OUTPUT"
