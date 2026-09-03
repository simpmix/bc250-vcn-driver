# VCN 3.0 Hardware Analysis & Security Architecture

Technical summary of the VCN 3.0 IP block on the AMD BC-250 ("Cyan Skillfish" / PS5 "Oberon" APU).

---

## Hardware Findings & Security Architecture

### 1. The Platform Security Processor (PSP) Hardware Root of Trust
On modern AMD APUs, the Video Core Next (VCN) IP block is an independent coprocessor featuring a dedicated microcontroller (VCPU) running firmware in private SRAM. The VCN cannot boot or execute instructions until the following sequence succeeds:
1. The host driver (`amdgpu`) sends a command via the PSP mailbox.
2. The AMD Platform Security Processor (an isolated on-die ARM security core with masked ROM and burned hardware eFuses) authenticates the signature on the VCN firmware blob (`vcn_3_0.bin`) using Sony's public root key.
3. Once authenticated, the PSP releases the hardware reset line and unmasks the VCPU clocks.

On the BC-250 SKU, the hardware eFuses were permanently set at the factory to mark the VCN block as unprovisioned/disabled. As a result, the PSP rejects VCN initialization with error `-1` (`amdgpu: psp vcn load failed: -22`).

### 2. SMN Bus & Dynamic IP Discovery vs. Static PCIe MMIO
* On RDNA 2 (Navi 2x / Oberon), registers are **not** statically mapped into the PCIe BAR address space at fixed offsets.
* All IP block communication is routed dynamically through the **System Management Network (SMN)** bus and configured via the GPU's binary **IP Discovery** table at boot.
* Attempting to perform arbitrary MMIO writes to offsets like `BAR + 0x7E00` does not reach the VCN block; it hits unrelated unmapped physical memory space, risking PCIe bus lockups and kernel panics.

### 3. Conclusion & Solution
Because the silicon VCN block is permanently locked by hardware eFuses and signed firmware requirements, the project uses **Approach 1: The Vulkan Compute VA-API Driver**. 

By utilizing the APU's **40 unlocked RDNA 2 Compute Units (2,560 stream processors)** to execute parallel compute shaders for motion estimation, transform, quantization, and entropy coding, we achieve high-performance hardware-like encoding without touching the locked VCN silicon.
