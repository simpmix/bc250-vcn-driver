# BC-250 Hardware Notes

## APU Specifications
- **Architecture**: Zen 2 CPU + RDNA 2 GPU
- **Compute Units**: 40 CUs
- **Hardware Block**: VCN 3.0 (Video Core Next)
- **Codename**: Cyan Skillfish

## Device Identification
- **PCI Vendor ID**: 1002 (AMD)
- **PCI Device ID**: 13fe
- **Revision**: Varies, commonly known as `CYAN_SKILLFISH_REV` in kernel code.

## Known Limitations
- The VCN block is physically present but locked by the Platform Security Processor (PSP) firmware, presumably a leftover from its original console design.
- The default DisplayPort/HDMI audio clock divisors in standard Linux drivers calculate the wrong frequencies, leading to distorted sound.

## Memory Map & Power
(Community research notes go here. Always ensure adequate cooling when stress testing.)

<!-- bc250-vcn-driver v0.1.0 -->
