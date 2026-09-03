# VCN 3.0 Register Map

Research notes on VCN 3.0 registers based on `vcn_v3_0.c` in the Linux kernel tree.

## Key Register Groups

### Power & Clock
- `mmUVD_POWER_STATUS`
- `mmUVD_CGC_CTRL`
These control clock gating and power states for the VCN block.

### Ring Buffer
- `mmUVD_RBC_RB_BASE`
- `mmUVD_RBC_RB_RPTR`
- `mmUVD_RBC_RB_WPTR`
Used for submitting commands to the VCN microcode.

### Status & Initialization
- `mmUVD_STATUS`
- `mmUVD_VCPU_CNTL`
Controls the VCN embedded CPU (VCPU) which runs the firmware. 
If firmware fails to load (due to PSP locks), the VCPU will not transition out of reset.

## Research Goals
Can we bypass the PSP validation by writing directly to `mmUVD_VCPU_CNTL` and sideloading firmware into the instruction cache, or is the memory bus strictly isolated by hardware fuses?
