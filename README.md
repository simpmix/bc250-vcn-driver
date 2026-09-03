# AMD BC-250 Custom Driver Project

## Overview
This project aims to develop custom drivers and workarounds for the AMD BC-250 (Cyan Skillfish) APU. This chip is a repurposed PS5 APU containing a Zen 2 CPU and an unlocked RDNA 2 GPU (40 CUs). However, the hardware VCN (Video Core Next) media encoder/decoder is disabled via firmware locks in Sony's PSP. 

This project explores three approaches to bring media encoding/decoding capabilities to the BC-250, as well as providing audio fixes for DisplayPort and HDMI output.

## Hardware Requirements
- AMD BC-250 mining board (PCI ID: 1002:13fe)
- Linux environment (Bazzite, CachyOS, or Fedora recommended)
- Root access for loading kernel modules and modifying firmware

## Quick Start
1. Run `tools/bc250_hw_probe.sh` to verify your hardware.
2. Apply the audio fix (see `audio-fix/README.md`) if you experience stuttering audio.
3. Explore the `approach3-firmware/` tools to dump and analyze your local VCN firmware.

## Architecture & Approaches

```
+-------------------------------------------------+
|               Linux Applications (OBS, etc)       |
+-----------------------+-------------------------+
                        |
              +---------v---------+
              | VA-API Interface  |
              +---------+---------+
                        |
       +----------------+-----------------+
       |                                  |
+------v------+                    +------v------+
| Compute     |                    | Hardware    |
| Encoder     |                    | VCN Bypass  |
| (Vulkan)    |                    | (Firmware)  |
+-------------+                    +-------------+
```

## Contributing
Contributions are welcome! Please follow standard Linux kernel coding styles for C code and include proper comments explaining hardware interaction.

## Community Resources
- [elektricM Docs](#)
- [SkillFishOS](#)

## License
Kernel module code is licensed under GPL-2.0.
Userspace tools and scripts are licensed under MIT.

## Disclaimer
**WARNING:** This project interfaces directly with hardware registers and modifies kernel modules. It is provided AS-IS. Incorrect register writes could potentially damage hardware or cause system instability. Use at your own risk.
