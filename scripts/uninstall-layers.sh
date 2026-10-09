#!/usr/bin/env bash
# uninstall-layers.sh — Remove gnumon Vulkan layers & tools from user profile (~/.local) and Flatpak

set -e

DEST_LIB="$HOME/.local/lib/gnumon"
DEST_BIN="$HOME/.local/bin"
DEST_IMPLICIT="$HOME/.local/share/vulkan/implicit_layer.d"
DEST_EXPLICIT="$HOME/.local/share/vulkan/explicit_layer.d"

echo "==> Removing Vulkan Layer manifests and layer libraries..."
rm -f "$DEST_IMPLICIT"/VkLayer_gnumon*.json
rm -f "$DEST_EXPLICIT"/VkLayer_gnumon*.json
rm -f "$DEST_IMPLICIT/libVkLayer_gnumon.so"
rm -f "$DEST_EXPLICIT/libVkLayer_gnumon.so"

# Flatpak Steam
FLATPAK_DIR="$HOME/.var/app/com.valvesoftware.Steam"
if [ -d "$FLATPAK_DIR" ]; then
    echo "==> Removing Flatpak Steam layer manifests..."
    rm -f "$FLATPAK_DIR/.local/share/vulkan/implicit_layer.d"/VkLayer_gnumon*.json
    rm -f "$FLATPAK_DIR/.local/share/vulkan/implicit_layer.d/libVkLayer_gnumon.so"
    rm -rf "$FLATPAK_DIR/.local/lib/gnumon"
fi

echo "==> Removing libraries..."
rm -rf "$DEST_LIB"
rm -f "$HOME/.local/lib/libpresentmon.so"*

echo "==> Removing executables..."
rm -f "$DEST_BIN/gnumon-run"
rm -f "$DEST_BIN/gnumon-cli"
rm -f "$DEST_BIN/gnumond"
rm -f "$DEST_BIN/gnumon-gui"

# Shared memory rings
rm -f /dev/shm/gnumon_* /dev/shm/gnumon_ring_* 2>/dev/null || true

echo "==> gnumon layers & tools have been completely removed from user profile."
