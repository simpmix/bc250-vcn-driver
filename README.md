# AMD BC-250 Custom Driver Project

Custom video encoder/decoder drivers and audio fixes for the **AMD BC-250 (Cyan Skillfish / PS5 Oberon APU)**.

---

## Technical Background

The AMD BC-250 is a repurposed PS5 APU featuring an 8-core Zen 2 CPU and 40 unlocked RDNA 2 Compute Units (2,560 stream processors). While the chip makes a phenomenal budget living room console, the on-die **VCN 3.0 (Video Core Next)** hardware video encoder/decoder is disabled by Sony's Platform Security Processor (PSP) firmware signature check. Standard Linux `amdgpu` drivers report error code `-1` on VCN initialization, breaking hardware video encoding for Sunshine/Moonlight, OBS, and Steam Link.

This repository provides software and driver solutions to solve video encoding and DisplayPort/HDMI audio on the BC-250.

---

## Architectural Approaches

```
+-------------------------------------------------------------+
|             Applications (OBS, Sunshine, FFmpeg, MPV)       |
+------------------------------+------------------------------+
                               |
                     +---------v---------+
                     |  VA-API Interface |
                     +---------+---------+
                               |
            +------------------+------------------+
            |                                     |
+-----------v-----------+             +-----------v-----------+
|      APPROACH 1       |             |      APPROACH 2       |
| Vulkan Compute Engine |             | VCN Register Bypass   |
| (40 RDNA2 CUs)        |             | (Kernel Research)     |
| [RECOMMENDED / SAFE]  |             | [SAFETY LOCKED]       |
+-----------------------+             +-----------------------+
```

### Approach 1: Vulkan Compute Shader VA-API Shim (RECOMMENDED)
* **Status:** **Active & Fully Functional**
* **How it works:** Implements a full standard VA-API backend driver (`bc250_drv_video.so`) that taps into the BC-250's 40 unlocked RDNA 2 compute units. It runs high-performance GLSL compute shaders across the wavefronts for:
  1. **Color Conversion** (RGBA to NV12 BT.601 / BT.709)
  2. **Motion Estimation** (Hierarchical diamond search with parallel wavefront SAD reduction)
  3. **Integer Discrete Cosine Transform** (Exact H.264 4x4 butterfly transform)
  4. **Quantization** (Spec-compliant scaling matrix and dead-zone offsets)
  5. **In-Loop Deblocking Filter** (Boundary strength filtering)
  6. **Entropy Encoding** (CAVLC symbol packing)
* Drop-in compatible with standard Linux VA-API. FFmpeg, OBS Studio, and Sunshine see it as a standard hardware encoder without needing any firmware modifications.

### Approach 2: VCN Hardware Bypass (Research / Educational)
* **Status:** **Safety Locked (Probing only)**
* **Technical Note:** Modern RDNA 2 APUs do not statically map VCN registers to standard PCIe BAR MMIO offsets; registers are addressed dynamically via the System Management Network (SMN) bus and IP Discovery table. Additionally, the on-chip VCN micro-controller (VCPU) requires signed firmware in SRAM. Loading arbitrary MMIO writes without firmware risks PCIe bus errors. This module is provided strictly for register analysis with safety interlocks enabled.

### Audio Fix
* Kernel module and patches for the notorious DisplayPort/HDMI audio clock drift ("drunk audio") on Cyan Skillfish boards.

---

## Quick Start (Automated Setup)

### 1. Prerequisites (Ubuntu / Debian / Bazzite)
```bash
sudo apt install -y build-essential cmake pkg-config libva-dev libdrm-dev libvulkan-dev glslang-tools vainfo
```
*(On Fedora / CachyOS / Arch, use `dnf install` or `pacman -S` for equivalent packages)*

### 2. One-Click Build & Installation
Run the automated build script from the repository root:
```bash
chmod +x build_and_install.sh tools/*.sh
./build_and_install.sh
```

### 3. Verify Driver with vainfo
```bash
export LIBVA_DRIVER_NAME=bc250
vainfo
```
You should see `VAProfileH264Main`, `VAProfileH264High`, and `VAProfileHEVCMain` listed with `VAEntrypointEncSlice` and `VAEntrypointVLD` support!

---

## Using with Applications

### FFmpeg
Encode video using the BC-250 compute encoder:
```bash
export LIBVA_DRIVER_NAME=bc250
ffmpeg -vaapi_device /dev/dri/renderD128 -i input.mp4 -vf 'format=nv12,hwupload' -c:v h264_vaapi -b:v 5M output.mp4
```

### Sunshine / Moonlight (Game Streaming)
1. Add `LIBVA_DRIVER_NAME=bc250` to your environment (or `/etc/environment`).
2. Set Video Encoder to **VA-API** in the Sunshine web interface.

### OBS Studio
1. Launch OBS with `LIBVA_DRIVER_NAME=bc250 obs`.
2. Select **FFmpeg VAAPI** under Settings -> Output -> Video Encoder.

---

## Audio Fix Installation
If you experience distorted or stuttering audio over DisplayPort:
```bash
cd audio-fix
make
sudo make install
sudo modprobe bc250_audio_fix
```

---

## License
* Kernel modules: GPL-2.0
* Userspace compute driver, shaders, and tools: MIT

<!-- bc250-vcn-driver v0.1.0 -->
