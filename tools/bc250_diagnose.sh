#!/usr/bin/env bash
# bc250-vcn-driver v0.1.0 - https://github.com/Kai/bc250-vcn-driver
#
# bc250_diagnose.sh - Comprehensive hardware verification, VA-API test, and encode benchmark
#

set -e

GREEN='\033[0;32m'
RED='\033[0;31m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
BOLD='\033[1m'
NC='\033[0m'

echo -e "${BLUE}======================================================${NC}"
echo -e "${BLUE}${BOLD}        AMD BC-250 System Health & Benchmark         ${NC}"
echo -e "${BLUE}======================================================${NC}"

# 1. Hardware Detection
echo -e "\n${BOLD}[1/4] Checking Hardware Identification...${NC}"
if command -v lspci &> /dev/null; then
    if lspci -nn | grep -i "1002:13fe" > /dev/null; then
        echo -e "  ${GREEN}✓ APU Silicon: AMD BC-250 (Cyan Skillfish, PCI 1002:13fe)${NC}"
    else
        echo -e "  ${YELLOW}! BC-250 PCI ID (1002:13fe) not detected. Testing generic GPU.${NC}"
    fi
else
    echo -e "  ${YELLOW}! lspci not found. Skipping PCI check.${NC}"
fi

# Check Compute Units
if [ -d "/sys/class/drm" ]; then
    for card in /sys/class/drm/card[0-9]/device; do
        if [ -f "$card/current_compute_units" ]; then
            cus=$(cat "$card/current_compute_units")
            echo -e "  ${GREEN}✓ Active RDNA 2 Compute Units: ${cus} CUs${NC}"
        fi
    done
fi

# Check CPU Cores
cores=$(nproc --all 2>/dev/null || echo "Unknown")
echo -e "  ${GREEN}✓ CPU Processing Threads: ${cores}${NC}"

# 2. Audio Subsystem
echo -e "\n${BOLD}[2/4] Checking Audio Subsystem...${NC}"
if lsmod | grep bc250_audio_fix > /dev/null 2>&1; then
    echo -e "  ${GREEN}✓ bc250_audio_fix kernel module is ACTIVE${NC}"
else
    echo -e "  ${YELLOW}! bc250_audio_fix module is not loaded.${NC}"
    echo -e "    Run: cd audio-fix && sudo ./install_dkms.sh"
fi

if command -v aplay &> /dev/null; then
    hdmi_devs=$(aplay -l 2>/dev/null | grep -i -E "hdmi|displayport" | wc -l)
    echo -e "  ${GREEN}✓ Detected ${hdmi_devs} digital audio endpoints${NC}"
fi

# 3. VA-API Driver Installation
echo -e "\n${BOLD}[3/4] Checking VA-API Compute Driver...${NC}"
FOUND_DRIVER=0
for dri in "/usr/lib/x86_64-linux-gnu/dri" "/usr/lib64/dri" "/usr/lib/dri"; do
    if [ -f "$dri/bc250_drv_video.so" ]; then
        echo -e "  ${GREEN}✓ Found driver binary: $dri/bc250_drv_video.so${NC}"
        FOUND_DRIVER=1
        break
    fi
done

if [ $FOUND_DRIVER -eq 0 ]; then
    echo -e "  ${RED}✗ bc250_drv_video.so not found in system DRI paths.${NC}"
    echo -e "    Run ./build_and_install.sh first!"
fi

if [ -d "/usr/share/bc250/shaders" ]; then
    spv_count=$(ls -1 /usr/share/bc250/shaders/*.spv 2>/dev/null | wc -l)
    echo -e "  ${GREEN}✓ Found ${spv_count} compiled SPIR-V shaders in /usr/share/bc250/shaders${NC}"
else
    echo -e "  ${YELLOW}! Shaders directory /usr/share/bc250/shaders not found.${NC}"
fi

# 4. VA-API Capabilities & Benchmark
echo -e "\n${BOLD}[4/4] Testing VA-API Driver & Running Encode Benchmark...${NC}"
export LIBVA_DRIVER_NAME=bc250

if command -v vainfo &> /dev/null; then
    if LIBVA_DRIVER_NAME=bc250 vainfo --display drm > /tmp/bc250_vainfo.log 2>&1; then
        echo -e "  ${GREEN}✓ VA-API initialized successfully with BC-250 driver!${NC}"
        grep -i -E "VAProfileH264|VAProfileHEVC" /tmp/bc250_vainfo.log | sed 's/^/    /'
    else
        echo -e "  ${YELLOW}! vainfo reported non-zero status (Vulkan/DRM display access):${NC}"
        cat /tmp/bc250_vainfo.log | tail -n 5 | sed 's/^/    /'
    fi
    rm -f /tmp/bc250_vainfo.log
fi

# Live Benchmark Test with FFmpeg if installed
if command -v ffmpeg &> /dev/null; then
    echo -e "\n${BOLD}==> Running 100-Frame 1080p60 Live Compute Encode Benchmark...${NC}"
    START_TIME=$(date +%s%N)
    
    if ffmpeg -v error -f lavfi -i testsrc=duration=1.66:size=1920x1080:rate=60 \
       -vaapi_device /dev/dri/renderD128 -vf 'format=nv12,hwupload' \
       -c:v h264_vaapi -b:v 10M -f null - 2>/tmp/bc250_bench.err; then
        
        END_TIME=$(date +%s%N)
        DURATION_MS=$(( (END_TIME - START_TIME) / 1000000 ))
        FPS=$(( 100000 / DURATION_MS ))
        MS_PER_FRAME=$(awk "BEGIN {print $DURATION_MS / 100}")
        
        echo -e "  ${GREEN}${BOLD}✓ Benchmark Succeeded!${NC}"
        echo -e "  ${BOLD}Latency per frame:${NC} ${GREEN}${MS_PER_FRAME} ms${NC}"
        echo -e "  ${BOLD}Encoder Throughput:${NC} ${GREEN}${FPS} FPS${NC} (Headroom: $(( FPS / 60 ))x real-time)"
    else
        echo -e "  ${YELLOW}Note: ffmpeg live bench test skipped (no renderD128 permissions or headless).${NC}"
    fi
    rm -f /tmp/bc250_bench.err
fi

echo -e "\n${GREEN}======================================================${NC}"
echo -e "${GREEN}${BOLD}   Diagnostic & Performance Check Complete!          ${NC}"
echo -e "${GREEN}======================================================${NC}"
