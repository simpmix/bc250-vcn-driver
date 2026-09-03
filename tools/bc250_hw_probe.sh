#!/bin/bash
# MIT License
# BC-250 Hardware Capability Probe

echo "========================================"
echo " AMD BC-250 (Cyan Skillfish) Probe Tool"
echo "========================================"

# Check PCI ID
if lspci -nn | grep -q "1002:13fe"; then
    echo "[+] BC-250 Hardware Detected!"
else
    echo "[-] BC-250 Hardware NOT Found! (Looking for 1002:13fe)"
    exit 1
fi

# Kernel info
echo ""
echo "--- System Info ---"
uname -r
echo "Mesa Version:"
glxinfo -B | grep "OpenGL version" || echo "Mesa not found or glxinfo missing"

# GPU info
echo ""
echo "--- GPU Info ---"
if [ -d "/sys/class/drm/card0/device" ]; then
    cat /sys/class/drm/card0/device/device | sed 's/^/Device ID: /'
    cat /sys/class/drm/card0/device/vendor | sed 's/^/Vendor ID: /'
    # Attempt to read CU count if possible (often via debugfs)
    echo "Check amdgpu_pm_info for clocks:"
    cat /sys/kernel/debug/dri/0/amdgpu_pm_info 2>/dev/null | grep -E "MHz|mW" || echo "Debugfs not accessible"
fi

# VCN Status
echo ""
echo "--- VCN Status ---"
if dmesg | grep -i "vcn" | grep -q "firmware"; then
    dmesg | grep -i "vcn" | grep "firmware"
else
    echo "No VCN firmware load messages in dmesg."
fi

# Audio
echo ""
echo "--- Audio Status ---"
aplay -l | grep -i "Generic" || echo "No generic audio device found."

# Vulkan
echo ""
echo "--- Vulkan Compute ---"
if command -v vulkaninfo &> /dev/null; then
    vulkaninfo | grep -i "compute" | head -n 5
else
    echo "vulkan-tools not installed"
fi
