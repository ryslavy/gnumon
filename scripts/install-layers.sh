#!/usr/bin/env bash
# install-layers.sh — Install gnumon Vulkan layers & tools into user profile (~/.local) without root

set -e

SOURCE_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

if [ -n "${APPDIR:-}" ] && [ -f "$APPDIR/usr/lib/libVkLayer_gnumon.so" ]; then
    LIB_DIR="$APPDIR/usr/lib"
    BIN_DIR="$APPDIR/usr/bin"
elif [ -f "$SOURCE_DIR/lib/libVkLayer_gnumon.so" ]; then
    LIB_DIR="$SOURCE_DIR/lib"
    BIN_DIR="$SOURCE_DIR/bin"
elif [ -f "$SOURCE_DIR/usr/lib/libVkLayer_gnumon.so" ]; then
    LIB_DIR="$SOURCE_DIR/usr/lib"
    BIN_DIR="$SOURCE_DIR/usr/bin"
elif [ -f "$SOURCE_DIR/build-container/libVkLayer_gnumon.so" ] && \
     { [ ! -f "$SOURCE_DIR/build-host/libVkLayer_gnumon.so" ] || \
       [ ! "$SOURCE_DIR/build-host/libVkLayer_gnumon.so" -nt "$SOURCE_DIR/build-container/libVkLayer_gnumon.so" ]; }; then
    # Prefer the GLIBC-portable container build only when it is not older than the host build
    LIB_DIR="$SOURCE_DIR/build-container"
    BIN_DIR="$SOURCE_DIR/build-host"
elif [ -f "$SOURCE_DIR/build-host/libVkLayer_gnumon.so" ]; then
    LIB_DIR="$SOURCE_DIR/build-host"
    BIN_DIR="$SOURCE_DIR/build-host"
elif [ -f "$SOURCE_DIR/build/libVkLayer_gnumon.so" ]; then
    LIB_DIR="$SOURCE_DIR/build"
    BIN_DIR="$SOURCE_DIR/build"
else
    echo "Error: libVkLayer_gnumon.so not found. Please compile first or run from extracted release package."
    exit 1
fi

DEST_LIB="$HOME/.local/lib/gnumon"
DEST_BIN="$HOME/.local/bin"
DEST_IMPLICIT="$HOME/.local/share/vulkan/implicit_layer.d"

mkdir -p "$DEST_LIB" "$DEST_BIN" "$DEST_IMPLICIT"

# Clean any legacy explicit layer manifests to ensure purely implicit operation
rm -f "$HOME/.local/share/vulkan/explicit_layer.d"/VkLayer_gnumon*.json 2>/dev/null || true
rm -f "$HOME/.local/share/vulkan/explicit_layer.d/libVkLayer_gnumon.so" 2>/dev/null || true

echo "==> Copying libraries to $DEST_LIB..."
cp -f "$LIB_DIR"/libVkLayer_gnumon.so "$DEST_LIB/"
if [ -f "$LIB_DIR"/libgnumon_gl.so ]; then
    cp -f "$LIB_DIR"/libgnumon_gl.so "$DEST_LIB/"
fi
if ls "$LIB_DIR"/libpresentmon.so* 1>/dev/null 2>&1; then
    cp -a "$LIB_DIR"/libpresentmon.so* "$DEST_LIB/"
    mkdir -p "$HOME/.local/lib"
    cp -a "$LIB_DIR"/libpresentmon.so* "$HOME/.local/lib/"
fi

echo "==> Copying executables to $DEST_BIN..."
cp -f "$BIN_DIR/gnumon-cli" "$DEST_BIN/"
cp -f "$BIN_DIR/gnumond" "$DEST_BIN/"
if [ -f "$BIN_DIR/gnumon-gui" ]; then
    cp -f "$BIN_DIR/gnumon-gui" "$DEST_BIN/"
fi
if [ -f "$SOURCE_DIR/scripts/gnumon-run" ]; then
    cp -f "$SOURCE_DIR/scripts/gnumon-run" "$DEST_BIN/"
elif [ -f "$BIN_DIR/gnumon-run" ]; then
    cp -f "$BIN_DIR/gnumon-run" "$DEST_BIN/"
fi
chmod +x "$DEST_BIN/gnumon-run" 2>/dev/null || true

echo "==> Generating Purely Implicit Vulkan Layer manifests..."

# 1. 64-bit Implicit Layer (automatic injection for all Vulkan/Wine/Proton/Zink games unless DISABLE_GNUMON=1)
cat <<EOF > "$DEST_IMPLICIT/VkLayer_gnumon.json"
{
    "file_format_version" : "1.0.0",
    "layer" : {
        "name": "VK_LAYER_GNUMON_capture",
        "type": "GLOBAL",
        "library_path": "$DEST_LIB/libVkLayer_gnumon.so",
        "api_version": "1.3.0",
        "implementation_version": "1",
        "description": "gnumon Linux PresentMon frame capture layer",
        "functions": {
            "vkNegotiateLoaderLayerInterfaceVersion": "vkNegotiateLoaderLayerInterfaceVersion"
        },
        "device_extensions": [
            {
                "name": "VK_KHR_swapchain",
                "spec_version": "70",
                "entrypoints": [
                    "vkCreateSwapchainKHR",
                    "vkDestroySwapchainKHR",
                    "vkGetSwapchainImagesKHR",
                    "vkAcquireNextImageKHR",
                    "vkQueuePresentKHR",
                    "vkAcquireNextImage2KHR"
                ]
            }
        ],
        "disable_environment": {
            "DISABLE_GNUMON": "1"
        }
    }
}
EOF

# Clean any duplicate or legacy manifests
rm -f "$DEST_IMPLICIT/VkLayer_gnumon.x86_64.json" "$DEST_IMPLICIT/VkLayer_gnumon.x86.json" 2>/dev/null || true

# Copy layer library directly to implicit directory for fallback loader resolution
cp -f "$DEST_LIB/libVkLayer_gnumon.so" "$DEST_IMPLICIT/"

# Check and copy 32-bit multilib layers (for 32-bit Proton / Wine games)
LIB32_DIR=""
for p in "$LIB_DIR/lib32" "$LIB_DIR/../lib32" "$SOURCE_DIR/build-host/lib32" "$SOURCE_DIR/build-container/lib32" "$SOURCE_DIR/lib32" "$SOURCE_DIR/usr/lib32"; do
    if [ -f "$p/libVkLayer_gnumon.so" ]; then
        LIB32_DIR="$p"
        break
    fi
done

if [ -n "$LIB32_DIR" ]; then
    echo "==> Found 32-bit multilib layers at $LIB32_DIR, installing..."
    mkdir -p "$DEST_LIB/lib32"
    cp -f "$LIB32_DIR/libVkLayer_gnumon.so" "$DEST_LIB/lib32/"
    if [ -f "$LIB32_DIR/libgnumon_gl.so" ]; then
        cp -f "$LIB32_DIR/libgnumon_gl.so" "$DEST_LIB/lib32/"
    fi

    # 32-bit Implicit Layer Manifest
    sed "s|\"$DEST_LIB/libVkLayer_gnumon.so\"|\"$DEST_LIB/lib32/libVkLayer_gnumon.so\"|g; s|\"VK_LAYER_GNUMON_capture\"|\"VK_LAYER_GNUMON_capture_32\"|g; s|\"gnumon Linux PresentMon frame capture layer\"|\"gnumon Linux PresentMon 32-bit frame capture layer\"|g" \
        "$DEST_IMPLICIT/VkLayer_gnumon.json" > "$DEST_IMPLICIT/VkLayer_gnumon.i686.json"
    echo "==> 32-bit multilib Vulkan implicit layer registered successfully!"
fi

# Register layer into Flatpak Steam if present
FLATPAK_DIR="$HOME/.var/app/com.valvesoftware.Steam"
if [ -d "$FLATPAK_DIR" ]; then
    echo "==> Registering layer for Flatpak Steam..."
    FLATPAK_IMPLICIT="$FLATPAK_DIR/.local/share/vulkan/implicit_layer.d"
    mkdir -p "$FLATPAK_IMPLICIT"
    cp -f "$DEST_LIB/libVkLayer_gnumon.so" "$FLATPAK_IMPLICIT/"
    sed "s|\"$DEST_LIB/libVkLayer_gnumon.so\"|\"$FLATPAK_IMPLICIT/libVkLayer_gnumon.so\"|g" "$DEST_IMPLICIT/VkLayer_gnumon.json" > "$FLATPAK_IMPLICIT/VkLayer_gnumon.json"
    if [ -f "$DEST_LIB/lib32/libVkLayer_gnumon.so" ]; then
        mkdir -p "$FLATPAK_DIR/.local/lib/gnumon/lib32"
        cp -f "$DEST_LIB/lib32/libVkLayer_gnumon.so" "$FLATPAK_DIR/.local/lib/gnumon/lib32/"
        sed "s|\"$DEST_LIB/lib32/libVkLayer_gnumon.so\"|\"$FLATPAK_DIR/.local/lib/gnumon/lib32/libVkLayer_gnumon.so\"|g" \
            "$DEST_IMPLICIT/VkLayer_gnumon.i686.json" > "$FLATPAK_IMPLICIT/VkLayer_gnumon.i686.json"
    fi
fi

echo "==> Successfully installed gnumon layer & tools!"
echo "Vulkan games on Steam will now be tracked automatically!"
echo "Or run games manually with:"
echo "   gnumon-run %command%"
echo "To disable for a specific game:"
echo "   DISABLE_GNUMON=1 %command%"
