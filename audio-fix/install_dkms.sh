#!/usr/bin/env bash
# bc250-vcn-driver v0.1.0 - https://github.com/Kai/bc250-vcn-driver
#
# install_dkms.sh - Installs bc250_audio_fix into DKMS for kernel auto-rebuilds
#

set -e

PKG_NAME="bc250-audio-fix"
PKG_VER="0.1.0"
SRC_DIR="/usr/src/${PKG_NAME}-${PKG_VER}"

if [ "$EUID" -ne 0 ]; then
    echo "Error: Please run as root (sudo ./install_dkms.sh)"
    exit 1
fi

if ! command -v dkms &> /dev/null; then
    echo "Error: dkms is not installed. Please install dkms first:"
    echo "  Ubuntu/Debian: sudo apt install dkms"
    echo "  Fedora:        sudo dnf install dkms"
    echo "  Arch:          sudo pacman -S dkms"
    exit 1
fi

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

echo "==> Preparing DKMS source directory: $SRC_DIR"
mkdir -p "$SRC_DIR"
cp -r "$SCRIPT_DIR"/* "$SRC_DIR/"

echo "==> Registering module with DKMS..."
if dkms status | grep "$PKG_NAME/$PKG_VER" > /dev/null 2>&1; then
    dkms remove "$PKG_NAME/$PKG_VER" --all || true
fi

dkms add "$PKG_NAME/$PKG_VER"
echo "==> Building module with DKMS..."
dkms build "$PKG_NAME/$PKG_VER"
echo "==> Installing module with DKMS..."
dkms install "$PKG_NAME/$PKG_VER"

# Auto-load on boot
echo "bc250_audio_fix" > /etc/modules-load.d/bc250_audio.conf 2>/dev/null || true
modprobe bc250_audio_fix 2>/dev/null || true

echo "✓ DKMS installation complete! The audio fix will now survive kernel upgrades automatically."
