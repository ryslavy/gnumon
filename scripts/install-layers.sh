#!/usr/bin/env bash
# install-layers.sh — Install gnumon Vulkan layers & tools into user profile (~/.local) without root

set -e

SOURCE_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

if [ -f "$SOURCE_DIR/lib/libVkLayer_gnumon.so" ]; then
    LIB_DIR="$SOURCE_DIR/lib"
    BIN_DIR="$SOURCE_DIR/bin"
elif [ -f "$SOURCE_DIR/build-container/libVkLayer_gnumon.so" ]; then
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
DEST_EXPLICIT="$HOME/.local/share/vulkan/explicit_layer.d"

mkdir -p "$DEST_LIB" "$DEST_BIN" "$DEST_IMPLICIT" "$DEST_EXPLICIT"

echo "==> Copying libraries to $DEST_LIB..."
cp -f "$LIB_DIR"/libVkLayer_gnumon.so "$DEST_LIB/"
if [ -f "$LIB_DIR"/libgnumon_gl.so ]; then
    cp -f "$LIB_DIR"/libgnumon_gl.so "$DEST_LIB/"
fi
if ls "$LIB_DIR"/libpresentmon.so* 1>/dev/null 2>&1; then
    cp -a "$LIB_DIR"/libpresentmon.so* "$DEST_LIB/"
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

echo "==> Generating Vulkan Layer manifests..."

# 1. Implicit Layer (active when ENABLE_GNUMON=1)
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
        "enable_environment": {
            "ENABLE_GNUMON": "1"
        },
        "disable_environment": {
            "DISABLE_GNUMON": "1"
        }
    }
}
EOF

# 2. Explicit Layer (active when explicitly named in VK_INSTANCE_LAYERS)
cat <<EOF > "$DEST_EXPLICIT/VkLayer_gnumon.json"
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
        }
    }
}
EOF

echo "==> Successfully installed gnumon layer & tools!"
echo "You can now run games with:"
echo "   gnumon-run %command%"
echo "or:"
echo "   ENABLE_GNUMON=1 %command%"
