#!/bin/bash
# Development install: compiles the plugin and installs it into the system.
# Usage: ./scripts/install-dev.sh   (asks for the sudo password when needed)

set -e

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="$ROOT_DIR/build"
SOURCE_DIR="$ROOT_DIR"
PLUGIN_DIR="/usr/lib/x86_64-linux-gnu/qt6/plugins"
NOTIFYRC_DIR="/usr/share/knotifications6"

echo "=== Dolphin SVN Plugin - development install ==="

# Build. 'cmake -S -B' creates and configures the build directory if it is
# missing (e.g. after it was cleaned), so this no longer fails when build/ is
# absent.
echo "[1/3] Compiling..."
cmake -S "$SOURCE_DIR" -B "$BUILD_DIR"
cmake --build "$BUILD_DIR" -j"$(nproc)"

# Install (needs root).
echo "[2/3] Installing into $PLUGIN_DIR ..."
if [ "$EUID" -ne 0 ]; then
    sudo mkdir -p "$PLUGIN_DIR/kf6/kfileitemaction"
    sudo mkdir -p "$PLUGIN_DIR/kf6/overlayicon"
    sudo mkdir -p "$NOTIFYRC_DIR"
    sudo cp "$BUILD_DIR/bin/kf6/kfileitemaction/dolphinsvnplugin.so" \
            "$PLUGIN_DIR/kf6/kfileitemaction/dolphinsvnplugin.so"
    sudo cp "$BUILD_DIR/bin/kf6/overlayicon/svnoverlayplugin.so" \
            "$PLUGIN_DIR/kf6/overlayicon/svnoverlayplugin.so"
    sudo cp "$SOURCE_DIR/plugin/dolphinsvnplugin.notifyrc" \
            "$NOTIFYRC_DIR/dolphinsvnplugin.notifyrc"
else
    mkdir -p "$PLUGIN_DIR/kf6/kfileitemaction"
    mkdir -p "$PLUGIN_DIR/kf6/overlayicon"
    mkdir -p "$NOTIFYRC_DIR"
    cp "$BUILD_DIR/bin/kf6/kfileitemaction/dolphinsvnplugin.so" \
       "$PLUGIN_DIR/kf6/kfileitemaction/dolphinsvnplugin.so"
    cp "$BUILD_DIR/bin/kf6/overlayicon/svnoverlayplugin.so" \
       "$PLUGIN_DIR/kf6/overlayicon/svnoverlayplugin.so"
    cp "$SOURCE_DIR/plugin/dolphinsvnplugin.notifyrc" \
       "$NOTIFYRC_DIR/dolphinsvnplugin.notifyrc"
fi

# Refresh the KDE plugin cache.
echo "[3/3] Refreshing the KDE plugin cache..."
kbuildsycoca6 --noincremental 2>/dev/null || true

echo ""
echo "Done. Please restart Dolphin:"
echo "  pkill dolphin 2>/dev/null; sleep 1; dolphin &"
echo ""
echo "Installed files:"
ls -la "$PLUGIN_DIR/kf6/kfileitemaction/dolphinsvnplugin.so"
ls -la "$PLUGIN_DIR/kf6/overlayicon/svnoverlayplugin.so"
ls -la "$NOTIFYRC_DIR/dolphinsvnplugin.notifyrc"
