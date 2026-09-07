#!/bin/bash
# Entfernt die per install-dev.sh manuell installierten Plugin-Dateien.
# Nötig z.B. bevor das Plugin sauber als Debian-Paket installiert wird.
# Aufruf: ./scripts/uninstall-dev.sh  (fragt bei Bedarf nach sudo-Passwort)

set -e

PLUGIN_DIR="/usr/lib/x86_64-linux-gnu/qt6/plugins"
NOTIFYRC_DIR="/usr/share/knotifications6"

FILES=(
    "$PLUGIN_DIR/kf6/kfileitemaction/dolphinsvnplugin.so"
    "$PLUGIN_DIR/kf6/overlayicon/svnoverlayplugin.so"
    "$NOTIFYRC_DIR/dolphinsvnplugin.notifyrc"
)

echo "=== Dolphin SVN Plugin - Entwicklungsinstallation entfernen ==="

SUDO=""
if [ "$EUID" -ne 0 ]; then
    SUDO="sudo"
fi

REMOVED=0
for f in "${FILES[@]}"; do
    if [ -e "$f" ]; then
        echo "Entferne $f"
        $SUDO rm "$f"
        REMOVED=$((REMOVED + 1))
    fi
done

if [ "$REMOVED" -eq 0 ]; then
    echo "Nichts zu tun — keine manuell installierten Dateien gefunden."
else
    echo "Aktualisiere KDE Plugin-Cache..."
    kbuildsycoca6 --noincremental 2>/dev/null || true
    echo ""
    echo "✓ $REMOVED Datei(en) entfernt. Dolphin neu starten:"
    echo "  pkill dolphin 2>/dev/null; sleep 1; dolphin &"
fi
