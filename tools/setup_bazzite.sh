#!/usr/bin/env bash
# bc250-vcn-driver v0.1.0 - https://github.com/Kai/bc250-vcn-driver
#
# setup_bazzite.sh - Automated installer tailored specifically for Bazzite / SteamOS / ChimeraOS (rpm-ostree)
#

set -e

GREEN='\033[0;32m'
RED='\033[0;31m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
BOLD='\033[1m'
NC='\033[0m'

echo -e "${BLUE}======================================================${NC}"
echo -e "${BLUE}${BOLD}      AMD BC-250 Installer for Bazzite / SteamOS     ${NC}"
echo -e "${BLUE}======================================================${NC}"

# Check for root
SUDO=""
if [ "$EUID" -ne 0 ]; then
    if command -v sudo &> /dev/null; then
        SUDO="sudo"
    else
        echo -e "${RED}Error: Please run with sudo.${NC}"
        exit 1
    fi
fi

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(dirname "$SCRIPT_DIR")"

echo -e "\n${BOLD}[1/4] Detecting Immutable OS Environment...${NC}"
IS_OSTREE=0
if [ -f "/run/ostree-booted" ] || command -v rpm-ostree &> /dev/null; then
    IS_OSTREE=1
    echo -e "  ${GREEN}✓ Detected rpm-ostree immutable filesystem (Bazzite / SteamOS / Silverblue)${NC}"
else
    echo -e "  ${YELLOW}! Standard writable filesystem detected.${NC}"
fi

# On rpm-ostree, /usr is read-only; /usr/local and /etc are writable
INSTALL_LIB_DIR="/usr/local/lib64/dri"
INSTALL_SHADER_DIR="/usr/local/share/bc250/shaders"

echo -e "\n${BOLD}[2/4] Installing VA-API Driver & Shaders to /usr/local...${NC}"
$SUDO mkdir -p "$INSTALL_LIB_DIR"
$SUDO mkdir -p "$INSTALL_SHADER_DIR"

# Build driver if needed
BUILD_DIR="$REPO_ROOT/approach1-compute-encoder/build"
if [ ! -f "$BUILD_DIR/bc250_drv_video.so" ]; then
    echo -e "  -> Building driver from source..."
    mkdir -p "$BUILD_DIR"
    (cd "$BUILD_DIR" && cmake .. -DCMAKE_BUILD_TYPE=Release && make -j"$(nproc)")
fi

# Copy driver to /usr/local/lib64/dri/ and standard fallback paths
echo -e "  -> Installing $INSTALL_LIB_DIR/bc250_drv_video.so"
$SUDO cp -f "$BUILD_DIR/bc250_drv_video.so" "$INSTALL_LIB_DIR/"
$SUDO mkdir -p "/usr/local/lib/dri"
$SUDO cp -f "$BUILD_DIR/bc250_drv_video.so" "/usr/local/lib/dri/"

# Copy shaders
if [ -d "$REPO_ROOT/approach1-compute-encoder/shaders" ]; then
    echo -e "  -> Copying shaders to $INSTALL_SHADER_DIR/"
    $SUDO cp -f "$BUILD_DIR"/*.spv "$INSTALL_SHADER_DIR/" 2>/dev/null || true
    $SUDO cp -f "$REPO_ROOT/approach1-compute-encoder/shaders"/*.comp "$INSTALL_SHADER_DIR/" 2>/dev/null || true
fi

echo -e "\n${BOLD}[3/4] Configuring Gamescope & System Environment...${NC}"
# Configure /etc/environment.d for systemd and Gamescope user session
$SUDO mkdir -p /etc/environment.d
cat << 'EOF' | $SUDO tee /etc/environment.d/99-bc250.conf > /dev/null
# AMD BC-250 VA-API Compute Encoder Configuration
LIBVA_DRIVER_NAME=bc250
LIBVA_DRIVERS_PATH=/usr/local/lib64/dri:/usr/local/lib/dri:/usr/lib64/dri
BC250_FAST_MODE=1
BC250_SHADER_DIR=/usr/local/share/bc250/shaders
EOF
echo -e "  ${GREEN}✓ Configured /etc/environment.d/99-bc250.conf${NC}"

# Add user to render group
CURRENT_USER="${SUDO_USER:-$USER}"
if [ -n "$CURRENT_USER" ] && [ "$CURRENT_USER" != "root" ]; then
    $SUDO usermod -a -G video,render "$CURRENT_USER" 2>/dev/null || true
    echo -e "  ${GREEN}✓ Granted GPU render permissions to user '$CURRENT_USER'${NC}"
fi

echo -e "\n${BOLD}[4/4] Setting Up DisplayPort Audio Fix...${NC}"
if [ -d "$REPO_ROOT/audio-fix" ]; then
    cd "$REPO_ROOT/audio-fix"
    if command -v dkms &> /dev/null; then
        $SUDO ./install_dkms.sh || echo -e "  ${YELLOW}! DKMS setup skipped.${NC}"
    else
        echo -e "  ${YELLOW}! DKMS not installed. Building module locally:${NC}"
        make || true
        $SUDO make install || true
        $SUDO modprobe bc250_audio_fix 2>/dev/null || true
    fi
    cd "$REPO_ROOT"
fi

echo -e "\n${GREEN}======================================================${NC}"
echo -e "${GREEN}${BOLD}   Bazzite / SteamOS Setup Completed Successfully!   ${NC}"
echo -e "${GREEN}======================================================${NC}"
echo -e "\nNext steps:"
echo -e "  1. Restart your Gamescope session or reboot your console."
echo -e "  2. Test the driver with: ${YELLOW}./tools/bc250_diagnose.sh${NC}"
echo -e "  3. In Sunshine Web UI: set Video Encoder to ${GREEN}VA-API${NC}."
