#!/usr/bin/env bash
# uninstall-layers.sh — Remove gnumon Vulkan layers & tools from user profile (~/.local)

set -e

DEST_LIB="$HOME/.local/lib/gnumon"
DEST_BIN="$HOME/.local/bin"
DEST_IMPLICIT="$HOME/.local/share/vulkan/implicit_layer.d"
DEST_EXPLICIT="$HOME/.local/share/vulkan/explicit_layer.d"

echo "==> Removing Vulkan Layer manifests..."
rm -f "$DEST_IMPLICIT/VkLayer_gnumon.json"
rm -f "$DEST_EXPLICIT/VkLayer_gnumon.json"

echo "==> Removing libraries..."
rm -rf "$DEST_LIB"

echo "==> Removing executables..."
rm -f "$DEST_BIN/gnumon-run"
rm -f "$DEST_BIN/gnumon-cli"
rm -f "$DEST_BIN/gnumond"
rm -f "$DEST_BIN/gnumon-gui"

echo "==> gnumon layers & tools have been completely removed from user profile."
