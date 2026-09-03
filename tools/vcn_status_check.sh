#!/bin/bash
# MIT License

echo "========================================"
echo " VCN Status Check Tool"
echo "========================================"

echo "[*] Checking dmesg for VCN..."
dmesg | grep -i vcn | tail -n 10

echo ""
echo "[*] Checking VA-API Status..."
if command -v vainfo &> /dev/null; then
    vainfo 2>&1 | grep -i -E "driver|profile|error"
else
    echo "vainfo is not installed. Please install libva-utils."
fi

echo ""
echo "[*] Checking VCN power state (if accessible)..."
if [ -f "/sys/kernel/debug/dri/0/amdgpu_pm_info" ]; then
    cat /sys/kernel/debug/dri/0/amdgpu_pm_info | grep -i vcn || echo "VCN power state not listed."
else
    echo "Debugfs not mounted or accessible."
fi

echo ""
echo "[*] Checking for custom BC-250 drivers..."
lsmod | grep bc250
