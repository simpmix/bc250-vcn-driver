#!/usr/bin/env bash
# bc250-vcn-driver v0.1.0 - https://github.com/Kai/bc250-vcn-driver
#
# uninstall_dkms.sh - Removes bc250_audio_fix from DKMS
#

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PKG_NAME="bc250-audio-fix"
PKG_VER=$(grep -m1 '^PACKAGE_VERSION=' "$SCRIPT_DIR/dkms.conf" 2>/dev/null | cut -d'"' -f2)
PKG_VER="${PKG_VER:-0.2.0}"
SRC_DIR="/usr/src/${PKG_NAME}-${PKG_VER}"

if [ "$EUID" -ne 0 ]; then
    echo "Error: Please run as root (sudo ./uninstall_dkms.sh)"
    exit 1
fi

echo "==> Removing $PKG_NAME/$PKG_VER from DKMS..."
dkms remove "$PKG_NAME/$PKG_VER" --all || true

if [ -d "$SRC_DIR" ]; then
    rm -rf "$SRC_DIR"
fi

rm -f /etc/modules-load.d/bc250_audio.conf
modprobe -r bc250_audio_fix 2>/dev/null || true

echo "✓ Uninstalled $PKG_NAME from DKMS."
