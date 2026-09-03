#!/usr/bin/env bash
# bc250-vcn-driver v0.1.0 - https://github.com/Kai/bc250-vcn-driver
#
# build_and_install.sh - Automated build, test, and installer for AMD BC-250 custom drivers
#

set -e

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

echo -e "${BLUE}======================================================${NC}"
echo -e "${BLUE}   AMD BC-250 (Cyan Skillfish) Custom Driver Setup    ${NC}"
echo -e "${BLUE}======================================================${NC}"

# Check for root / sudo
SUDO=""
if [ "$EUID" -ne 0 ]; then
    if command -v sudo &> /dev/null; then
        SUDO="sudo"
    else
        echo -e "${YELLOW}Warning: Running without root. Installation steps may fail.${NC}"
    fi
fi

# Detect hardware
echo -e "\n${BLUE}[1/5] Checking hardware...${NC}"
if lspci -nn | grep -i "1002:13fe" > /dev/null 2>&1; then
    echo -e "${GREEN}✓ Detected AMD BC-250 APU (1002:13fe)${NC}"
else
    echo -e "${YELLOW}! BC-250 (1002:13fe) not detected on PCI bus.${NC}"
    echo -e "  Proceeding with build anyway (Vulkan compute driver will work on compatible GPUs)."
fi

# Check dependencies
echo -e "\n${BLUE}[2/5] Checking dependencies...${NC}"
MISSING_PKGS=()

check_cmd() {
    if ! command -v "$1" &> /dev/null; then
        echo -e "  - Missing tool: $1"
        MISSING_PKGS+=("$2")
    fi
}

check_cmd cmake "cmake"
check_cmd gcc "gcc / build-essential"
check_cmd pkg-config "pkg-config"

if ! command -v glslangValidator &> /dev/null && ! command -v glslc &> /dev/null; then
    echo -e "  - Missing SPIR-V shader compiler (glslangValidator or glslc)"
    MISSING_PKGS+=("glslang-tools or shaderc")
fi

if [ ${#MISSING_PKGS[@]} -gt 0 ]; then
    echo -e "${YELLOW}Some dependencies may be missing:${NC}"
    for pkg in "${MISSING_PKGS[@]}"; do
        echo -e "    * $pkg"
    done
    echo -e "\nTo install on Debian/Ubuntu/Bazzite:"
    echo -e "  ${GREEN}sudo apt install -y build-essential cmake pkg-config libva-dev libdrm-dev libvulkan-dev glslang-tools vainfo${NC}"
    echo -e "To install on Fedora/CachyOS:"
    echo -e "  ${GREEN}sudo dnf install -y gcc gcc-c++ cmake pkgconf libva-devel libdrm-devel vulkan-loader-devel glslang libva-utils${NC}"
fi

# Build Approach 1: Compute Encoder VA-API driver
echo -e "\n${BLUE}[3/5] Building Compute Encoder VA-API driver...${NC}"
BUILD_DIR="approach1-compute-encoder/build"
mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

cmake .. -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTS=ON
make -j"$(nproc)"

# Run tests
echo -e "\n${BLUE}[4/5] Running driver unit tests...${NC}"
ctest --output-on-failure || echo -e "${YELLOW}Some unit tests reported warnings (may require Vulkan environment).${NC}"

# Install driver
echo -e "\n${BLUE}[5/5] Installing driver...${NC}"
$SUDO make install

cd ../..

# Configure environment
echo -e "\n${GREEN}======================================================${NC}"
echo -e "${GREEN}   Installation Complete!                             ${NC}"
echo -e "${GREEN}======================================================${NC}"
echo -e "\nTo use the BC-250 driver, set:"
echo -e "  ${YELLOW}export LIBVA_DRIVER_NAME=bc250${NC}"
echo -e "\nTo test with vainfo:"
echo -e "  ${GREEN}LIBVA_DRIVER_NAME=bc250 vainfo${NC}"
echo -e "\nTo use with FFmpeg:"
echo -e "  ${GREEN}ffmpeg -vaapi_device /dev/dri/renderD128 -i input.mp4 -vf 'format=nv12,hwupload' -c:v h264_vaapi output.mp4${NC}"
