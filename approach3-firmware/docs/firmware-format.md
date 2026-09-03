# AMD VCN Firmware Format

This document describes the structure of AMD Video Core Next (VCN) firmware blobs based on analysis of the `amdgpu` driver source code (specifically `amdgpu_ucode.h` and `vcn_v3_0.c`).

## Common Firmware Header

All recent AMD GPU firmware blobs share a common header structure. The header format is public and well-defined in the Linux kernel:

```c
struct common_firmware_header {
    uint32_t size_bytes;              // Total size of the blob (header + microcode)
    uint32_t header_size_bytes;       // Size of this header
    uint16_t header_version_major;    // Header version
    uint16_t header_version_minor;
    uint16_t ip_version_major;        // IP Block version (e.g., VCN version)
    uint16_t ip_version_minor;
    uint32_t ucode_version;           // Microcode version
    uint32_t ucode_size_bytes;        // Size of the microcode array
    uint32_t ucode_array_offset_bytes;// Offset to the microcode array
    uint32_t crc32;                   // CRC32 checksum of the microcode
} __packed;
```

## VCN Specific Additions

For VCN firmwares, the `ucode_array_offset_bytes` points to the start of the actual VCN microcode. VCN 3.0 (Cyan Skillfish) requires firmware to be loaded into a specific memory pool mapped for the PSP (Platform Security Processor) to validate.

Since Cyan Skillfish's VCN is locked by Sony's PSP configuration, replacing the firmware directly usually fails the PSP validation step. This approach (Firmware Research) focuses on identifying precisely *how* the PSP locks it down and whether custom headers or signature spoofing could bypass this block.

## Analyzing cyan_skillfish_vcn.bin

When inspecting the official firmware blob:
1. We parse the common header.
2. We extract the `ucode_data`.
3. The remaining data may contain signatures or PSP-specific metadata.
