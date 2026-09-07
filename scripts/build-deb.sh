#!/bin/bash
# Builds a Debian package for the Dolphin SVN Plugin.
# Usage: ./scripts/build-deb.sh [version]   (default: version from CMakeLists.txt, no root)
# Result: release/dolphin-svn-plugin_<version>_<arch>.deb in the project directory.

set -e

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

# The version is defined once in the top-level CMakeLists.txt (project(... VERSION x.y.z)).
# Read it from there so the package name never drifts from the compiled version.
# An explicit argument still wins, e.g. for a test build.
CMAKE_VERSION="$(sed -n 's/^[[:space:]]*project([^)]*VERSION[[:space:]]\+\([0-9][0-9.]*\).*/\1/p' \
    "$ROOT_DIR/CMakeLists.txt" | head -n1)"
VERSION="${1:-${CMAKE_VERSION:-1.0.0}}"
ARCH="$(dpkg --print-architecture)"
PKG_NAME="dolphin-svn-plugin"

BUILD_DIR="$ROOT_DIR/build-deb/cmake"
PKG_DIR="$ROOT_DIR/build-deb/pkg"
PLUGIN_SUBDIR="usr/lib/x86_64-linux-gnu/qt6/plugins"

echo "=== $PKG_NAME $VERSION ($ARCH) ==="

# --- 1. Release build ---
echo "[1/4] Compiling (Release)..."
# Capture the configure and build output (including the harmless "not a git
# repository" messages the KDE CMake modules print while project/ is not yet a
# git repository) into a log and show it only on an actual failure.
BUILD_LOG="$(mktemp)"
trap 'rm -f "$BUILD_LOG"' EXIT
run_quiet() {
    if ! "$@" > "$BUILD_LOG" 2>&1; then
        echo "Failed: $*" >&2
        cat "$BUILD_LOG" >&2
        exit 1
    fi
}
run_quiet cmake -S "$ROOT_DIR" -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Release
run_quiet cmake --build "$BUILD_DIR" -j"$(nproc)"

# --- 2. Stage the package tree ---
echo "[2/4] Staging package contents..."
rm -rf "$PKG_DIR"
mkdir -p "$PKG_DIR/$PLUGIN_SUBDIR/kf6/kfileitemaction"
mkdir -p "$PKG_DIR/$PLUGIN_SUBDIR/kf6/overlayicon"
mkdir -p "$PKG_DIR/usr/share/knotifications6"
mkdir -p "$PKG_DIR/usr/share/metainfo"
mkdir -p "$PKG_DIR/DEBIAN"

cp "$BUILD_DIR/bin/kf6/kfileitemaction/dolphinsvnplugin.so" \
   "$PKG_DIR/$PLUGIN_SUBDIR/kf6/kfileitemaction/"
cp "$BUILD_DIR/bin/kf6/overlayicon/svnoverlayplugin.so" \
   "$PKG_DIR/$PLUGIN_SUBDIR/kf6/overlayicon/"
cp "$ROOT_DIR/plugin/dolphinsvnplugin.notifyrc" \
   "$PKG_DIR/usr/share/knotifications6/"
# AppStream metadata so software centres (KDE Discover) show the author, the
# licence, the name and the summary. Without it they display "Unknown".
cp "$ROOT_DIR/plugin/io.github.form1c.dolphin-svn-plugin.metainfo.xml" \
   "$PKG_DIR/usr/share/metainfo/"

# Machine-readable copyright file (Debian DEP-5). This is where the author and
# the licence belong in a .deb, there is no "License" control field. Installed
# to the standard location /usr/share/doc/<pkg>/copyright.
DOC_DIR="$PKG_DIR/usr/share/doc/$PKG_NAME"
mkdir -p "$DOC_DIR"
cat > "$DOC_DIR/copyright" <<EOF
Format: https://www.debian.org/doc/packaging-manuals/copyright-format/1.0/
Upstream-Name: $PKG_NAME
Source: https://github.com/form1c/Dolphin-SVN-Plugin

Files: *
Copyright: 2026 formic
License: MIT

License: MIT
 Permission is hereby granted, free of charge, to any person obtaining a copy
 of this software and associated documentation files (the "Software"), to deal
 in the Software without restriction, including without limitation the rights
 to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 copies of the Software, and to permit persons to whom the Software is
 furnished to do so, subject to the following conditions:
 .
 The above copyright notice and this permission notice shall be included in all
 copies or substantial portions of the Software.
 .
 THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 SOFTWARE.
EOF

# --- 3. Derive dependencies with dpkg-shlibdeps ---
echo "[3/4] Detecting dependencies..."
# dpkg-shlibdeps reads the ELF NEEDED entries of the two plugins and resolves
# them to the correct runtime packages WITH minimum-version constraints, and
# only the libraries actually needed. This replaces the earlier ldd + dpkg -S
# sweep, which listed every transitive library by its exact package name and so
# only installed on the exact build system. It needs a minimal debian/control in
# the working directory. 'subversion' is added on top because the plugin runs the
# 'svn' command at runtime, which is not a linked library and cannot be detected
# from the binaries.
SHLIB_DIR="$ROOT_DIR/build-deb/shlibdeps"
rm -rf "$SHLIB_DIR"
mkdir -p "$SHLIB_DIR/debian"
cat > "$SHLIB_DIR/debian/control" <<EOF
Source: $PKG_NAME
Package: $PKG_NAME
Architecture: any
EOF
LIB_DEPENDS="$(cd "$SHLIB_DIR" && dpkg-shlibdeps -O --ignore-missing-info \
    "$PKG_DIR/$PLUGIN_SUBDIR/kf6/kfileitemaction/dolphinsvnplugin.so" \
    "$PKG_DIR/$PLUGIN_SUBDIR/kf6/overlayicon/svnoverlayplugin.so" 2>/dev/null \
    | sed 's/^shlibs:Depends=//')"
rm -rf "$SHLIB_DIR"
if [ -z "$LIB_DEPENDS" ]; then
    echo "dpkg-shlibdeps produced no dependencies. Is dpkg-dev installed?" >&2
    exit 1
fi
DEPENDS="subversion, $LIB_DEPENDS"

cat > "$PKG_DIR/DEBIAN/control" <<EOF
Package: $PKG_NAME
Version: $VERSION
Section: kde
Priority: optional
Architecture: $ARCH
Depends: $DEPENDS
Maintainer: formic <apps@unifyzer.de>
Homepage: https://github.com/form1c/Dolphin-SVN-Plugin
Description: TortoiseSVN-like Subversion integration for KDE Dolphin
 Context-menu actions (update, commit, log, diff, blame, merge,
 repository browser, ...) and status overlay icons for Subversion
 working copies in the Dolphin file manager.
EOF

# --- 4. Build the package ---
echo "[4/4] Building package..."
mkdir -p "$ROOT_DIR/release"
DEB_FILE="$ROOT_DIR/release/${PKG_NAME}_${VERSION}_${ARCH}.deb"
dpkg-deb --root-owner-group --build "$PKG_DIR" "$DEB_FILE" > /dev/null
rm -rf "$PKG_DIR" # the staging tree is no longer needed after the package is built

echo ""
echo "✓ Package created: $DEB_FILE"
echo ""
echo "Install:    sudo apt install \"$DEB_FILE\""
echo "Uninstall:  sudo apt remove $PKG_NAME"
echo ""
echo "Note: remove any previous manual installation first:"
echo "  ./scripts/uninstall-dev.sh"
